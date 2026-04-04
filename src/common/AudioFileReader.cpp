#include "AudioFileReader.h"

#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <algorithm>
#include <atomic>
#include <future>
#include <thread>
#include <cmath>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#elif defined(Q_OS_MAC)
#include <CoreFoundation/CoreFoundation.h>
#include <AudioToolbox/AudioToolbox.h>
#endif

#ifdef Q_OS_MAC
namespace {
QString formatOsStatusMessage(OSStatus status)
{
    return QStringLiteral("0x%1")
        .arg(static_cast<quint32>(status), 8, 16, QChar('0'));
}
}
#endif

// ===== 公開API =====

bool AudioFileReader::isSupportedAudioFile(const QString& filePath)
{
    QString lower = filePath.toLower();
    return lower.endsWith(".wav") || lower.endsWith(".mp3") || lower.endsWith(".m4a");
}

AudioFileData AudioFileReader::readFile(const QString& filePath)
{
    QString lower = filePath.toLower();

    if (lower.endsWith(".wav")) {
        return readWav(filePath);
    }
    if (lower.endsWith(".mp3") || lower.endsWith(".m4a")) {
        return readCompressedAudio(filePath);
    }

    AudioFileData data;
    data.errorMessage = QStringLiteral("未対応のファイル形式: %1").arg(filePath);
    return data;
}

QVector<float> AudioFileReader::generateWaveformPreview(
    const QVector<float>& samplesL,
    const QVector<float>& samplesR,
    int previewWidth)
{
    if (samplesL.isEmpty() || previewWidth <= 0) return {};

    const int totalSamples = samplesL.size();
    QVector<float> preview(previewWidth, 0.0f);

    auto fillPreviewRange = [&](int beginIndex, int endIndex) {
        for (int i = beginIndex; i < endIndex; ++i) {
            int startIdx = static_cast<int>(static_cast<qint64>(i) * totalSamples / previewWidth);
            int endIdxLocal = static_cast<int>(static_cast<qint64>(i + 1) * totalSamples / previewWidth);
            endIdxLocal = qMin(endIdxLocal, totalSamples);

            float peak = 0.0f;
            for (int j = startIdx; j < endIdxLocal; ++j) {
                const float valL = std::abs(samplesL[j]);
                const float valR = samplesR.isEmpty() ? valL : std::abs(samplesR[j]);
                const float maxVal = qMax(valL, valR);
                if (maxVal > peak) {
                    peak = maxVal;
                }
            }
            preview[i] = peak;
        }
    };

    const int minParallelWidth = 256;
    const unsigned int hwThreads = (std::max)(1u, std::thread::hardware_concurrency());
    const int workerCount = std::min<int>(previewWidth, static_cast<int>(hwThreads));
    if (workerCount <= 1 || previewWidth < minParallelWidth) {
        fillPreviewRange(0, previewWidth);
        return preview;
    }

    static std::atomic<int> s_parallelPreviewJobs{0};
    const int previousJobCount = s_parallelPreviewJobs.fetch_add(1, std::memory_order_relaxed);
    if (previousJobCount > 0) {
        s_parallelPreviewJobs.fetch_sub(1, std::memory_order_relaxed);
        fillPreviewRange(0, previewWidth);
        return preview;
    }

    std::vector<std::future<void>> futures;
    futures.reserve(static_cast<size_t>(workerCount));
    const int chunkSize = (previewWidth + workerCount - 1) / workerCount;

    // 波形プレビューは表示点ごとに独立しているため、区間単位で並列集計できる。
    for (int workerIndex = 0; workerIndex < workerCount; ++workerIndex) {
        const int beginIndex = workerIndex * chunkSize;
        const int endIndex = qMin(previewWidth, beginIndex + chunkSize);
        if (beginIndex >= endIndex) {
            break;
        }

        futures.emplace_back(std::async(std::launch::async, [&, beginIndex, endIndex]() {
            fillPreviewRange(beginIndex, endIndex);
        }));
    }

    for (std::future<void>& future : futures) {
        future.get();
    }
    s_parallelPreviewJobs.fetch_sub(1, std::memory_order_relaxed);

    return preview;
}

// ===== WAVファイル手動パース =====

AudioFileData AudioFileReader::readWav(const QString& filePath)
{
    AudioFileData result;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorMessage = QStringLiteral("ファイルを開けません: %1").arg(filePath);
        return result;
    }

    QByteArray data = file.readAll();
    file.close();

    if (data.size() < 44) {
        result.errorMessage = QStringLiteral("WAVファイルが小さすぎます");
        return result;
    }

    const char* ptr = data.constData();

    // RIFFヘッダー確認
    if (memcmp(ptr, "RIFF", 4) != 0 || memcmp(ptr + 8, "WAVE", 4) != 0) {
        result.errorMessage = QStringLiteral("無効なWAVファイルヘッダー");
        return result;
    }

    // fmtチャンクを探す
    int pos = 12;
    int audioFormat = 0;
    int numChannels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;
    bool fmtFound = false;

    while (pos + 8 <= data.size()) {
        char chunkId[5] = {};
        memcpy(chunkId, ptr + pos, 4);
        uint32_t chunkSize = 0;
        memcpy(&chunkSize, ptr + pos + 4, 4);

        if (memcmp(chunkId, "fmt ", 4) == 0) {
            if (pos + 8 + 16 > data.size()) break;
            memcpy(&audioFormat, ptr + pos + 8, 2);
            memcpy(&numChannels, ptr + pos + 10, 2);
            memcpy(&sampleRate, ptr + pos + 12, 4);
            // byteRate at +16 (skip)
            // blockAlign at +20 (skip)
            memcpy(&bitsPerSample, ptr + pos + 22, 2);
            fmtFound = true;
        }

        if (memcmp(chunkId, "data", 4) == 0 && fmtFound) {
            int dataStart = pos + 8;
            int dataSize = qMin(static_cast<int>(chunkSize),
                                data.size() - dataStart);

            if (audioFormat == 1) {
                // PCM
                int bytesPerSample = bitsPerSample / 8;
                int totalSamples = dataSize / bytesPerSample;
                int numFrames = totalSamples / numChannels;

                result.samplesL.resize(numFrames);
                if (numChannels >= 2) result.samplesR.resize(numFrames);

                const char* src = ptr + dataStart;

                for (int f = 0; f < numFrames; ++f) {
                    for (int ch = 0; ch < numChannels; ++ch) {
                        float sample = 0.0f;
                        int srcOffset = (f * numChannels + ch) * bytesPerSample;
                        if (srcOffset + bytesPerSample > dataSize) break;

                        if (bitsPerSample == 16) {
                            int16_t val = 0;
                            memcpy(&val, src + srcOffset, 2);
                            sample = val / 32768.0f;
                        } else if (bitsPerSample == 24) {
                            int32_t val = 0;
                            memcpy(reinterpret_cast<char*>(&val), src + srcOffset, 3);
                            if (val & 0x800000) val |= 0xFF000000; // 符号拡張
                            sample = val / 8388608.0f;
                        } else if (bitsPerSample == 32) {
                            // 32bit float
                            memcpy(&sample, src + srcOffset, 4);
                        } else if (bitsPerSample == 8) {
                            uint8_t val = static_cast<uint8_t>(src[srcOffset]);
                            sample = (val - 128) / 128.0f;
                        }

                        if (ch == 0) result.samplesL[f] = sample;
                        else if (ch == 1) result.samplesR[f] = sample;
                        // 3ch以降は無視
                    }
                }

                // モノラルの場合は右チャンネルを左と同じにする
                if (numChannels == 1) {
                    result.samplesR = result.samplesL;
                }

                result.sampleRate = sampleRate;
                result.channels = numChannels;
                result.valid = true;

            } else if (audioFormat == 3) {
                // IEEE float
                int bytesPerSample = bitsPerSample / 8;
                int totalSamples = dataSize / bytesPerSample;
                int numFrames = totalSamples / numChannels;

                result.samplesL.resize(numFrames);
                if (numChannels >= 2) result.samplesR.resize(numFrames);

                const char* src = ptr + dataStart;

                for (int f = 0; f < numFrames; ++f) {
                    for (int ch = 0; ch < numChannels; ++ch) {
                        float sample = 0.0f;
                        int srcOffset = (f * numChannels + ch) * bytesPerSample;
                        if (srcOffset + bytesPerSample > dataSize) break;

                        if (bitsPerSample == 32) {
                            memcpy(&sample, src + srcOffset, 4);
                        } else if (bitsPerSample == 64) {
                            double dval = 0.0;
                            memcpy(&dval, src + srcOffset, 8);
                            sample = static_cast<float>(dval);
                        }

                        if (ch == 0) result.samplesL[f] = sample;
                        else if (ch == 1) result.samplesR[f] = sample;
                    }
                }

                if (numChannels == 1) {
                    result.samplesR = result.samplesL;
                }

                result.sampleRate = sampleRate;
                result.channels = numChannels;
                result.valid = true;

            } else {
                // 非PCMフォーマット → OSネイティブデコーダで展開
                return readCompressedAudio(filePath);
            }

            break;
        }

        pos += 8 + chunkSize;
        if (chunkSize % 2 != 0) pos++; // パディング
    }

    if (!result.valid) {
        result.errorMessage = QStringLiteral("WAVデータチャンクが見つかりません");
        return result;
    }

    // 波形プレビューを生成
    result.waveformPreview = generateWaveformPreview(
        result.samplesL, result.samplesR, 2048);

    qDebug() << QStringLiteral("WAVファイル読み込み完了: %1 (%2 frames, %3 Hz, %4ch)")
                .arg(filePath)
                .arg(result.samplesL.size())
                .arg(result.sampleRate)
                .arg(result.channels);

    return result;
}

// ===== 圧縮音声デコード =====

AudioFileData AudioFileReader::readCompressedAudio(const QString& filePath)
{
    AudioFileData result;

#ifdef Q_OS_WIN
    // COM / MF 初期化
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool comInitialized = SUCCEEDED(hr) || hr == S_FALSE;

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        result.errorMessage = QStringLiteral("Media Foundation初期化エラー: 0x%1").arg(hr, 8, 16, QChar('0'));
        if (comInitialized) CoUninitialize();
        return result;
    }

    // Source Readerを作成
    IMFSourceReader* reader = nullptr;
    std::wstring wpath = filePath.toStdWString();
    hr = MFCreateSourceReaderFromURL(wpath.c_str(), nullptr, &reader);
    if (FAILED(hr) || !reader) {
        result.errorMessage = QStringLiteral("ファイルを開けません: %1").arg(filePath);
        MFShutdown();
        if (comInitialized) CoUninitialize();
        return result;
    }

    // 出力フォーマットをPCM float 44100Hz stereoに設定
    IMFMediaType* outputType = nullptr;
    MFCreateMediaType(&outputType);
    outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    outputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    outputType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
    outputType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    outputType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
    outputType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 8); // 2ch * 4bytes
    outputType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 44100 * 8);

    hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, outputType);
    outputType->Release();

    if (FAILED(hr)) {
        result.errorMessage = QStringLiteral("メディアタイプ設定エラー");
        reader->Release();
        MFShutdown();
        if (comInitialized) CoUninitialize();
        return result;
    }

    // デコード結果を蓄積
    QVector<float> interleavedSamples;
    interleavedSamples.reserve(44100 * 2 * 60); // 約1分分を予約

    // 全サンプルを読み出す
    for (;;) {
        DWORD flags = 0;
        IMFSample* sample = nullptr;
        hr = reader->ReadSample(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            0, nullptr, &flags, nullptr, &sample);

        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            if (sample) sample->Release();
            break;
        }

        if (!sample) continue;

        IMFMediaBuffer* buffer = nullptr;
        sample->ConvertToContiguousBuffer(&buffer);
        if (buffer) {
            BYTE* audioData = nullptr;
            DWORD dataLen = 0;
            buffer->Lock(&audioData, nullptr, &dataLen);

            int floatCount = dataLen / sizeof(float);
            const float* floatData = reinterpret_cast<const float*>(audioData);
            int oldSize = interleavedSamples.size();
            interleavedSamples.resize(oldSize + floatCount);
            memcpy(interleavedSamples.data() + oldSize, floatData, dataLen);

            buffer->Unlock();
            buffer->Release();
        }
        sample->Release();
    }

    reader->Release();
    MFShutdown();
    if (comInitialized) CoUninitialize();

    if (interleavedSamples.isEmpty()) {
        result.errorMessage = QStringLiteral("デコード結果が空です");
        return result;
    }

    // インターリーブ → デインターリーブ（2ch）
    int numFrames = interleavedSamples.size() / 2;
    result.samplesL.resize(numFrames);
    result.samplesR.resize(numFrames);

    for (int i = 0; i < numFrames; ++i) {
        result.samplesL[i] = interleavedSamples[i * 2];
        result.samplesR[i] = interleavedSamples[i * 2 + 1];
    }

    result.sampleRate = 44100.0;
    result.channels = 2;
    result.valid = true;

    // 波形プレビューを生成
    result.waveformPreview = generateWaveformPreview(
        result.samplesL, result.samplesR, 2048);

    qDebug() << QStringLiteral("MFデコード完了: %1 (%2 frames, %3 Hz)")
                .arg(filePath)
                .arg(numFrames)
                .arg(result.sampleRate);

#elif defined(Q_OS_MAC)
    const QByteArray nativePath = QFile::encodeName(QFileInfo(filePath).absoluteFilePath());
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(nativePath.constData()),
        nativePath.size(),
        false);
    if (!url) {
        result.errorMessage = QStringLiteral("ファイルURLを作成できません: %1").arg(filePath);
        return result;
    }

    ExtAudioFileRef audioFile = nullptr;
    OSStatus status = ExtAudioFileOpenURL(url, &audioFile);
    CFRelease(url);

    if (status != noErr || !audioFile) {
        result.errorMessage = QStringLiteral("ExtAudioFileOpenURL 失敗: %1")
            .arg(formatOsStatusMessage(status));
        return result;
    }

    AudioStreamBasicDescription fileFormat {};
    UInt32 propertySize = sizeof(fileFormat);
    status = ExtAudioFileGetProperty(audioFile,
                                     kExtAudioFileProperty_FileDataFormat,
                                     &propertySize,
                                     &fileFormat);
    if (status != noErr) {
        result.errorMessage = QStringLiteral("ファイルフォーマット取得失敗: %1")
            .arg(formatOsStatusMessage(status));
        ExtAudioFileDispose(audioFile);
        return result;
    }

    AudioStreamBasicDescription clientFormat {};
    clientFormat.mSampleRate = fileFormat.mSampleRate > 0.0 ? fileFormat.mSampleRate : 44100.0;
    clientFormat.mFormatID = kAudioFormatLinearPCM;
    clientFormat.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
    clientFormat.mBytesPerPacket = static_cast<UInt32>(sizeof(float) * 2);
    clientFormat.mFramesPerPacket = 1;
    clientFormat.mBytesPerFrame = static_cast<UInt32>(sizeof(float) * 2);
    clientFormat.mChannelsPerFrame = 2;
    clientFormat.mBitsPerChannel = 32;

    status = ExtAudioFileSetProperty(audioFile,
                                     kExtAudioFileProperty_ClientDataFormat,
                                     sizeof(clientFormat),
                                     &clientFormat);
    if (status != noErr) {
        result.errorMessage = QStringLiteral("クライアントフォーマット設定失敗: %1")
            .arg(formatOsStatusMessage(status));
        ExtAudioFileDispose(audioFile);
        return result;
    }

    SInt64 fileLengthFrames = 0;
    propertySize = sizeof(fileLengthFrames);
    if (ExtAudioFileGetProperty(audioFile,
                                kExtAudioFileProperty_FileLengthFrames,
                                &propertySize,
                                &fileLengthFrames) != noErr) {
        fileLengthFrames = 0;
    }

    QVector<float> interleavedSamples;
    if (fileLengthFrames > 0) {
        interleavedSamples.reserve(static_cast<int>(fileLengthFrames * 2));
    }

    constexpr UInt32 chunkFrames = 4096;
    std::vector<float> chunkBuffer(static_cast<size_t>(chunkFrames) * 2u, 0.0f);

    for (;;) {
        UInt32 framesToRead = chunkFrames;
        AudioBufferList bufferList {};
        bufferList.mNumberBuffers = 1;
        bufferList.mBuffers[0].mNumberChannels = 2;
        bufferList.mBuffers[0].mDataByteSize = static_cast<UInt32>(chunkBuffer.size() * sizeof(float));
        bufferList.mBuffers[0].mData = chunkBuffer.data();

        status = ExtAudioFileRead(audioFile, &framesToRead, &bufferList);
        if (status != noErr) {
            result.errorMessage = QStringLiteral("ExtAudioFileRead 失敗: %1")
                .arg(formatOsStatusMessage(status));
            ExtAudioFileDispose(audioFile);
            return result;
        }
        if (framesToRead == 0) {
            break;
        }

        const int sampleCount = static_cast<int>(framesToRead * 2);
        const int oldSize = interleavedSamples.size();
        interleavedSamples.resize(oldSize + sampleCount);
        std::memcpy(interleavedSamples.data() + oldSize,
                    chunkBuffer.data(),
                    static_cast<size_t>(sampleCount) * sizeof(float));
    }

    ExtAudioFileDispose(audioFile);

    if (interleavedSamples.isEmpty()) {
        result.errorMessage = QStringLiteral("デコード結果が空です");
        return result;
    }

    const int numFrames = interleavedSamples.size() / 2;
    result.samplesL.resize(numFrames);
    result.samplesR.resize(numFrames);

    for (int i = 0; i < numFrames; ++i) {
        result.samplesL[i] = interleavedSamples[i * 2];
        result.samplesR[i] = interleavedSamples[i * 2 + 1];
    }

    result.sampleRate = clientFormat.mSampleRate;
    result.channels = 2;
    result.valid = true;
    result.waveformPreview = generateWaveformPreview(
        result.samplesL, result.samplesR, 2048);

    qDebug() << QStringLiteral("ExtAudioFileデコード完了: %1 (%2 frames, %3 Hz)")
                .arg(filePath)
                .arg(numFrames)
                .arg(result.sampleRate);

#else
    result.errorMessage = QStringLiteral("MP3/M4AデコードはこのOSでは未対応です");
#endif

    return result;
}
