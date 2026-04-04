#include "AudioInputCaptureBackend.h"

#include <QDebug>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#elif defined(Q_OS_MAC)
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>
#endif

#ifdef Q_OS_MAC
namespace {
AudioDeviceID defaultInputDevice()
{
    AudioDeviceID deviceId = kAudioObjectUnknown;
    UInt32 size = sizeof(deviceId);
    AudioObjectPropertyAddress address {
        kAudioHardwarePropertyDefaultInputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    if (AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                   &address,
                                   0,
                                   nullptr,
                                   &size,
                                   &deviceId) != noErr) {
        return kAudioObjectUnknown;
    }
    return deviceId;
}

double deviceSampleRate(AudioDeviceID deviceId)
{
    Float64 sampleRate = 44100.0;
    UInt32 size = sizeof(sampleRate);
    AudioObjectPropertyAddress address {
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    if (AudioObjectGetPropertyData(deviceId, &address, 0, nullptr, &size, &sampleRate) != noErr ||
        sampleRate <= 0.0) {
        return 44100.0;
    }
    return static_cast<double>(sampleRate);
}

UInt32 deviceBufferSize(AudioDeviceID deviceId)
{
    UInt32 frameCount = 512;
    UInt32 size = sizeof(frameCount);
    AudioObjectPropertyAddress address {
        kAudioDevicePropertyBufferFrameSize,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    if (AudioObjectGetPropertyData(deviceId, &address, 0, nullptr, &size, &frameCount) != noErr ||
        frameCount == 0) {
        return 512;
    }
    return frameCount;
}

UInt32 deviceInputChannels(AudioDeviceID deviceId)
{
    AudioObjectPropertyAddress address {
        kAudioDevicePropertyStreamConfiguration,
        kAudioDevicePropertyScopeInput,
        kAudioObjectPropertyElementMain
    };

    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(deviceId, &address, 0, nullptr, &size) != noErr || size == 0) {
        return 2;
    }

    std::vector<UInt8> storage(size, 0);
    auto* bufferList = reinterpret_cast<AudioBufferList*>(storage.data());
    if (AudioObjectGetPropertyData(deviceId, &address, 0, nullptr, &size, bufferList) != noErr) {
        return 2;
    }

    UInt32 channels = 0;
    for (UInt32 i = 0; i < bufferList->mNumberBuffers; ++i) {
        channels += bufferList->mBuffers[i].mNumberChannels;
    }
    return channels == 0 ? 2 : channels;
}

QString macAudioError(OSStatus status)
{
    return QStringLiteral("0x%1")
        .arg(static_cast<quint32>(status), 8, 16, QChar('0'));
}
}
#endif

namespace {

void reportBackendError(const AudioInputCaptureBackend::Context& context, const QString& message)
{
    if (context.errorReporter) {
        context.errorReporter(message);
    }
}

#ifdef Q_OS_WIN
struct CaptureSampleFormatInfo {
    bool supported = false;
    bool isFloat = false;
    bool isUnsignedPcm = false;
    int bytesPerSample = 0;
    int validBitsPerSample = 0;
    int blockAlign = 0;
};

CaptureSampleFormatInfo describeCaptureSampleFormat(const WAVEFORMATEX* format)
{
    CaptureSampleFormatInfo info;
    if (!format || format->nChannels == 0) {
        return info;
    }

    info.blockAlign = static_cast<int>(format->nBlockAlign);
    info.bytesPerSample = format->nBlockAlign > 0
        ? static_cast<int>(format->nBlockAlign / format->nChannels)
        : static_cast<int>((format->wBitsPerSample + 7) / 8);
    if (info.blockAlign <= 0 && info.bytesPerSample > 0) {
        info.blockAlign = info.bytesPerSample * static_cast<int>(format->nChannels);
    }
    info.validBitsPerSample = static_cast<int>(format->wBitsPerSample);

    const bool isExtensible = format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    const auto* extensible = isExtensible
        ? reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format)
        : nullptr;

    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (extensible && extensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
        info.isFloat = true;
        info.supported =
            (info.bytesPerSample == static_cast<int>(sizeof(float)) &&
             info.validBitsPerSample == 32) ||
            (info.bytesPerSample == static_cast<int>(sizeof(double)) &&
             info.validBitsPerSample == 64);
        return info;
    }

    if (format->wFormatTag == WAVE_FORMAT_PCM ||
        (extensible && extensible->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)) {
        if (extensible && extensible->Samples.wValidBitsPerSample > 0) {
            info.validBitsPerSample = static_cast<int>(extensible->Samples.wValidBitsPerSample);
        }
        info.isUnsignedPcm =
            info.bytesPerSample == 1 && info.validBitsPerSample <= 8;
        info.supported =
            info.bytesPerSample >= 1 &&
            info.bytesPerSample <= static_cast<int>(sizeof(std::int32_t)) &&
            info.validBitsPerSample > 0 &&
            info.validBitsPerSample <= info.bytesPerSample * 8;
    }

    return info;
}

std::int64_t signExtendLittleEndianSample(const unsigned char* src, int bytesPerSample)
{
    std::uint64_t rawValue = 0;
    for (int byteIndex = 0; byteIndex < bytesPerSample; ++byteIndex) {
        rawValue |= static_cast<std::uint64_t>(src[byteIndex]) << (byteIndex * 8);
    }

    const int totalBits = bytesPerSample * 8;
    if (totalBits < 64 &&
        (rawValue & (std::uint64_t{1} << (totalBits - 1))) != 0) {
        rawValue |= (~std::uint64_t{0}) << totalBits;
    }
    return static_cast<std::int64_t>(rawValue);
}

float readPcmSample(const unsigned char* src,
                    int bytesPerSample,
                    int validBitsPerSample,
                    bool isUnsignedPcm)
{
    if (!src || bytesPerSample <= 0 || validBitsPerSample <= 0) {
        return 0.0f;
    }

    if (isUnsignedPcm) {
        return static_cast<float>((static_cast<int>(src[0]) - 128) / 128.0);
    }

    std::int64_t sampleValue = signExtendLittleEndianSample(src, bytesPerSample);
    const int containerBits = bytesPerSample * 8;
    const int shift = std::max(0, containerBits - validBitsPerSample);
    if (shift > 0) {
        sampleValue >>= shift;
    }

    const double scale = std::ldexp(1.0, validBitsPerSample - 1);
    return std::clamp(static_cast<float>(sampleValue / scale), -1.0f, 1.0f);
}

float readFloatSample(const unsigned char* src, int bytesPerSample)
{
    if (!src) {
        return 0.0f;
    }

    if (bytesPerSample == static_cast<int>(sizeof(float))) {
        float value = 0.0f;
        std::memcpy(&value, src, sizeof(value));
        return value;
    }
    if (bytesPerSample == static_cast<int>(sizeof(double))) {
        double value = 0.0;
        std::memcpy(&value, src, sizeof(value));
        return static_cast<float>(value);
    }
    return 0.0f;
}
#endif

class UnsupportedAudioInputCaptureBackend final : public AudioInputCaptureBackend
{
public:
    explicit UnsupportedAudioInputCaptureBackend(Context context)
        : m_context(std::move(context))
    {
    }

    bool initialize() override
    {
        const QString message = QStringLiteral("AudioInputCapture: このOS向けの入力バックエンドは未実装です");
        qWarning() << message;
        reportBackendError(m_context, message);
        return false;
    }

    bool start() override { return false; }
    void stop() override {}
    bool isRunning() const override { return false; }
    double sampleRate() const override { return 44100.0; }

private:
    Context m_context;
};

#ifdef Q_OS_WIN
class WasapiAudioInputCaptureBackend final : public AudioInputCaptureBackend
{
public:
    explicit WasapiAudioInputCaptureBackend(Context context)
        : m_context(std::move(context))
    {
    }

    ~WasapiAudioInputCaptureBackend() override
    {
        stop();
        cleanup();
    }

    bool initialize() override
    {
        if (m_initialized) {
            return true;
        }

        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator),
                                      reinterpret_cast<void**>(&m_enumerator));
        if (FAILED(hr)) {
            qWarning() << "AudioInputCapture: デバイス列挙子の作成に失敗:" << hr;
            return false;
        }

        hr = m_enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &m_device);
        if (FAILED(hr)) {
            qWarning() << "AudioInputCapture: デフォルト入力デバイスの取得に失敗:" << hr;
            cleanup();
            return false;
        }

        hr = m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(&m_audioClient));
        if (FAILED(hr)) {
            qWarning() << "AudioInputCapture: IAudioClient の取得に失敗:" << hr;
            cleanup();
            return false;
        }

        WAVEFORMATEX* mixFormat = nullptr;
        hr = m_audioClient->GetMixFormat(&mixFormat);
        if (FAILED(hr) || !mixFormat) {
            qWarning() << "AudioInputCapture: ミックスフォーマットの取得に失敗:" << hr;
            cleanup();
            return false;
        }

        m_mixFormat = mixFormat;
        const CaptureSampleFormatInfo sampleFormat = describeCaptureSampleFormat(m_mixFormat);
        if (!sampleFormat.supported) {
            const QString message = QStringLiteral(
                "AudioInputCapture: この入力デバイスの共有モード形式には未対応です");
            qWarning() << message
                       << "BitsPerSample:" << m_mixFormat->wBitsPerSample
                       << "BlockAlign:" << m_mixFormat->nBlockAlign
                       << "FormatTag:" << m_mixFormat->wFormatTag;
            reportBackendError(m_context, message);
            cleanup();
            return false;
        }

        m_sampleRate = mixFormat->nSamplesPerSec;
        m_numChannels = std::max<int>(1, mixFormat->nChannels);
        m_captureFormatIsFloat = sampleFormat.isFloat;
        m_captureFormatIsUnsignedPcm = sampleFormat.isUnsignedPcm;
        m_captureBytesPerSample = sampleFormat.bytesPerSample;
        m_captureValidBitsPerSample = sampleFormat.validBitsPerSample;
        m_captureBlockAlign = sampleFormat.blockAlign;

        m_eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!m_eventHandle) {
            qWarning() << "AudioInputCapture: イベントハンドルの作成に失敗";
            cleanup();
            return false;
        }

        const REFERENCE_TIME requestedDuration = 100000; // 10ms
        hr = m_audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            requestedDuration,
            0,
            reinterpret_cast<const WAVEFORMATEX*>(m_mixFormat),
            nullptr);

        if (FAILED(hr)) {
            qWarning() << "AudioInputCapture: オーディオクライアントの初期化に失敗:" << hr;
            cleanup();
            return false;
        }

        hr = m_audioClient->SetEventHandle(m_eventHandle);
        if (FAILED(hr)) {
            qWarning() << "AudioInputCapture: イベントハンドル設定に失敗:" << hr;
            cleanup();
            return false;
        }

        UINT32 bufferFrames = 0;
        hr = m_audioClient->GetBufferSize(&bufferFrames);
        if (FAILED(hr)) {
            qWarning() << "AudioInputCapture: バッファサイズ取得に失敗:" << hr;
            cleanup();
            return false;
        }
        m_bufferSize = static_cast<int>(bufferFrames);

        hr = m_audioClient->GetService(__uuidof(IAudioCaptureClient),
                                       reinterpret_cast<void**>(&m_captureClient));
        if (FAILED(hr)) {
            qWarning() << "AudioInputCapture: IAudioCaptureClient の取得に失敗:" << hr;
            cleanup();
            return false;
        }

        m_initialized = true;
        qDebug() << "AudioInputCapture: 初期化完了 - SampleRate:" << m_sampleRate
                 << "Channels:" << m_numChannels
                 << "BytesPerSample:" << m_captureBytesPerSample
                 << "ValidBitsPerSample:" << m_captureValidBitsPerSample
                 << "BufferSize:" << m_bufferSize;
        return true;
    }

    bool start() override
    {
        if (!m_initialized && !initialize()) {
            return false;
        }

        if (m_running.load()) {
            return true;
        }

        HRESULT hr = m_audioClient->Start();
        if (FAILED(hr)) {
            qWarning() << "AudioInputCapture: キャプチャ開始に失敗:" << hr;
            return false;
        }

        m_running.store(true);
        m_thread = QThread::create([this]() { captureThread(); });
        m_thread->start();
        m_thread->setPriority(QThread::TimeCriticalPriority);
        return true;
    }

    void stop() override
    {
        if (!m_running.load()) {
            return;
        }

        m_running.store(false);

        if (m_eventHandle) {
            SetEvent(m_eventHandle);
        }

        if (m_thread) {
            if (!m_thread->wait(100)) {
                qWarning() << "AudioInputCapture: キャプチャスレッド終了待機がタイムアウト";
            }
            delete m_thread;
            m_thread = nullptr;
        }

        if (m_audioClient) {
            m_audioClient->Stop();
            m_audioClient->Reset();
        }
    }

    bool isRunning() const override { return m_running.load(); }
    double sampleRate() const override { return m_sampleRate; }

private:
    void captureThread()
    {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        std::vector<float> tempLeft;
        std::vector<float> tempRight;

        while (m_running.load()) {
            const DWORD waitResult = WaitForSingleObject(m_eventHandle, 100);
            if (!m_running.load()) {
                break;
            }
            if (waitResult != WAIT_OBJECT_0) {
                continue;
            }

            UINT32 packetLength = 0;
            HRESULT hr = m_captureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) {
                qWarning() << "AudioInputCapture: GetNextPacketSize に失敗:" << hr;
                continue;
            }

            while (packetLength > 0 && m_running.load()) {
                BYTE* data = nullptr;
                UINT32 numFrames = 0;
                DWORD flags = 0;

                hr = m_captureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
                if (FAILED(hr)) {
                    qWarning() << "AudioInputCapture: GetBuffer に失敗:" << hr;
                    break;
                }

                tempLeft.resize(numFrames);
                tempRight.resize(numFrames);

                if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || !data) {
                    std::fill(tempLeft.begin(), tempLeft.end(), 0.0f);
                    std::fill(tempRight.begin(), tempRight.end(), 0.0f);
                } else {
                    // 実デバイス形式はばらつくため、ここで必ず内部 float L/R に正規化する。
                    convertCaptureBuffer(reinterpret_cast<const unsigned char*>(data),
                                         static_cast<int>(numFrames),
                                         tempLeft, tempRight);
                }

                if (m_context.captureInvoker && numFrames > 0) {
                    m_context.captureInvoker(tempLeft.data(), tempRight.data(),
                                            static_cast<int>(numFrames), m_sampleRate);
                }

                m_captureClient->ReleaseBuffer(numFrames);

                hr = m_captureClient->GetNextPacketSize(&packetLength);
                if (FAILED(hr)) {
                    qWarning() << "AudioInputCapture: GetNextPacketSize に失敗:" << hr;
                    break;
                }
            }
        }

        CoUninitialize();
    }

    void convertCaptureBuffer(const unsigned char* data, int numFrames,
                              std::vector<float>& outL,
                              std::vector<float>& outR) const
    {
        if (!m_mixFormat || !data || numFrames <= 0) {
            return;
        }

        const int channels = std::max(1, m_numChannels);
        const int rightChannelIndex = std::min(1, channels - 1);

        for (int frame = 0; frame < numFrames; ++frame) {
            float left = 0.0f;
            float right = 0.0f;
            const unsigned char* framePtr =
                data + (static_cast<size_t>(frame) * static_cast<size_t>(m_captureBlockAlign));
            const unsigned char* leftPtr = framePtr;
            const unsigned char* rightPtr =
                framePtr + (static_cast<size_t>(rightChannelIndex) *
                            static_cast<size_t>(m_captureBytesPerSample));

            // WAVEFORMATEXTENSIBLE の valid bits を見て、見かけの 32bit コンテナにも対応する。
            if (m_captureFormatIsFloat) {
                left = readFloatSample(leftPtr, m_captureBytesPerSample);
                right = readFloatSample(rightPtr, m_captureBytesPerSample);
            } else {
                left = readPcmSample(leftPtr,
                                     m_captureBytesPerSample,
                                     m_captureValidBitsPerSample,
                                     m_captureFormatIsUnsignedPcm);
                right = readPcmSample(rightPtr,
                                      m_captureBytesPerSample,
                                      m_captureValidBitsPerSample,
                                      m_captureFormatIsUnsignedPcm);
            }

            if (channels == 1) {
                right = left;
            }

            outL[frame] = left;
            outR[frame] = right;
        }
    }

    void cleanup()
    {
        if (m_captureClient) {
            m_captureClient->Release();
            m_captureClient = nullptr;
        }
        if (m_audioClient) {
            m_audioClient->Release();
            m_audioClient = nullptr;
        }
        if (m_device) {
            m_device->Release();
            m_device = nullptr;
        }
        if (m_enumerator) {
            m_enumerator->Release();
            m_enumerator = nullptr;
        }
        if (m_mixFormat) {
            CoTaskMemFree(m_mixFormat);
            m_mixFormat = nullptr;
        }
        if (m_eventHandle) {
            CloseHandle(m_eventHandle);
            m_eventHandle = nullptr;
        }
        m_captureFormatIsFloat = true;
        m_captureFormatIsUnsignedPcm = false;
        m_captureBytesPerSample = static_cast<int>(sizeof(float));
        m_captureValidBitsPerSample = 32;
        m_captureBlockAlign = 0;
        m_initialized = false;
    }

    Context m_context;
    IMMDeviceEnumerator* m_enumerator = nullptr;
    IMMDevice* m_device = nullptr;
    IAudioClient* m_audioClient = nullptr;
    IAudioCaptureClient* m_captureClient = nullptr;
    WAVEFORMATEX* m_mixFormat = nullptr;
    HANDLE m_eventHandle = nullptr;
    QThread* m_thread = nullptr;
    std::atomic<bool> m_running{false};
    double m_sampleRate = 44100.0;
    int m_numChannels = 2;
    int m_bufferSize = 0;
    bool m_captureFormatIsFloat = true;
    bool m_captureFormatIsUnsignedPcm = false;
    int m_captureBytesPerSample = static_cast<int>(sizeof(float));
    int m_captureValidBitsPerSample = 32;
    int m_captureBlockAlign = 0;
    bool m_initialized = false;
};
#endif

#ifdef Q_OS_MAC
class MacAudioInputCaptureBackend final : public AudioInputCaptureBackend
{
public:
    explicit MacAudioInputCaptureBackend(Context context)
        : m_context(std::move(context))
    {
    }

    ~MacAudioInputCaptureBackend() override
    {
        stop();
        cleanup();
    }

    bool initialize() override
    {
        if (m_initialized) {
            return true;
        }

        m_inputDeviceId = defaultInputDevice();
        if (m_inputDeviceId == kAudioObjectUnknown) {
            qWarning() << "AudioInputCapture: デフォルト入力デバイスの取得に失敗";
            return false;
        }

        m_sampleRate = deviceSampleRate(m_inputDeviceId);
        m_numChannels = std::clamp<int>(static_cast<int>(deviceInputChannels(m_inputDeviceId)), 1, 2);
        m_bufferSize = static_cast<int>(deviceBufferSize(m_inputDeviceId));

        AudioComponentDescription description {};
        description.componentType = kAudioUnitType_Output;
        description.componentSubType = kAudioUnitSubType_HALOutput;
        description.componentManufacturer = kAudioUnitManufacturer_Apple;

        AudioComponent component = AudioComponentFindNext(nullptr, &description);
        if (!component) {
            qWarning() << "AudioInputCapture: HAL AudioUnit が見つかりません";
            return false;
        }

        OSStatus status = AudioComponentInstanceNew(component, &m_audioUnit);
        if (status != noErr || !m_audioUnit) {
            qWarning() << "AudioInputCapture: AudioUnit 作成に失敗:" << macAudioError(status);
            cleanup();
            return false;
        }

        UInt32 enableInput = 1;
        UInt32 disableOutput = 0;
        status = AudioUnitSetProperty(m_audioUnit,
                                      kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Input,
                                      1,
                                      &enableInput,
                                      sizeof(enableInput));
        if (status != noErr) {
            qWarning() << "AudioInputCapture: 入力有効化に失敗:" << macAudioError(status);
            cleanup();
            return false;
        }

        status = AudioUnitSetProperty(m_audioUnit,
                                      kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Output,
                                      0,
                                      &disableOutput,
                                      sizeof(disableOutput));
        if (status != noErr) {
            qWarning() << "AudioInputCapture: 出力無効化に失敗:" << macAudioError(status);
            cleanup();
            return false;
        }

        status = AudioUnitSetProperty(m_audioUnit,
                                      kAudioOutputUnitProperty_CurrentDevice,
                                      kAudioUnitScope_Global,
                                      0,
                                      &m_inputDeviceId,
                                      sizeof(m_inputDeviceId));
        if (status != noErr) {
            qWarning() << "AudioInputCapture: 入力デバイス設定に失敗:" << macAudioError(status);
            cleanup();
            return false;
        }

        AudioStreamBasicDescription streamFormat {};
        streamFormat.mSampleRate = m_sampleRate;
        streamFormat.mFormatID = kAudioFormatLinearPCM;
        streamFormat.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
        streamFormat.mBytesPerPacket = static_cast<UInt32>(sizeof(float) * m_numChannels);
        streamFormat.mFramesPerPacket = 1;
        streamFormat.mBytesPerFrame = static_cast<UInt32>(sizeof(float) * m_numChannels);
        streamFormat.mChannelsPerFrame = static_cast<UInt32>(m_numChannels);
        streamFormat.mBitsPerChannel = 32;

        status = AudioUnitSetProperty(m_audioUnit,
                                      kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Output,
                                      1,
                                      &streamFormat,
                                      sizeof(streamFormat));
        if (status != noErr) {
            qWarning() << "AudioInputCapture: ストリームフォーマット設定に失敗:" << macAudioError(status);
            cleanup();
            return false;
        }

        AURenderCallbackStruct callback {};
        callback.inputProc = &MacAudioInputCaptureBackend::inputCallback;
        callback.inputProcRefCon = this;
        status = AudioUnitSetProperty(m_audioUnit,
                                      kAudioOutputUnitProperty_SetInputCallback,
                                      kAudioUnitScope_Global,
                                      0,
                                      &callback,
                                      sizeof(callback));
        if (status != noErr) {
            qWarning() << "AudioInputCapture: 入力コールバック設定に失敗:" << macAudioError(status);
            cleanup();
            return false;
        }

        status = AudioUnitInitialize(m_audioUnit);
        if (status != noErr) {
            qWarning() << "AudioInputCapture: AudioUnit 初期化に失敗:" << macAudioError(status);
            cleanup();
            return false;
        }

        UInt32 maxFramesPerSlice = 0;
        UInt32 propertySize = sizeof(maxFramesPerSlice);
        if (AudioUnitGetProperty(m_audioUnit,
                                 kAudioUnitProperty_MaximumFramesPerSlice,
                                 kAudioUnitScope_Global,
                                 0,
                                 &maxFramesPerSlice,
                                 &propertySize) == noErr &&
            maxFramesPerSlice > 0) {
            m_bufferSize = std::max(m_bufferSize, static_cast<int>(maxFramesPerSlice));
        }

        m_interleavedBuffer.assign(static_cast<size_t>(m_bufferSize * m_numChannels), 0.0f);
        m_captureBufferL.assign(static_cast<size_t>(m_bufferSize), 0.0f);
        m_captureBufferR.assign(static_cast<size_t>(m_bufferSize), 0.0f);
        m_initialized = true;

        qDebug() << "AudioInputCapture: macOS初期化完了 - SampleRate:" << m_sampleRate
                 << "Channels:" << m_numChannels
                 << "BufferSize:" << m_bufferSize;
        return true;
    }

    bool start() override
    {
        if (!m_initialized && !initialize()) {
            return false;
        }

        if (m_running.load()) {
            return true;
        }

        const OSStatus status = AudioOutputUnitStart(m_audioUnit);
        if (status != noErr) {
            qWarning() << "AudioInputCapture: AudioUnit 開始に失敗:" << macAudioError(status);
            return false;
        }

        m_running.store(true);
        return true;
    }

    void stop() override
    {
        if (!m_running.load()) {
            return;
        }

        m_running.store(false);
        if (m_audioUnit) {
            const OSStatus status = AudioOutputUnitStop(m_audioUnit);
            if (status != noErr) {
                qWarning() << "AudioInputCapture: AudioUnit 停止に失敗:" << macAudioError(status);
            }
        }
    }

    bool isRunning() const override { return m_running.load(); }
    double sampleRate() const override { return m_sampleRate; }

private:
    static OSStatus inputCallback(void* inRefCon,
                                  AudioUnitRenderActionFlags* ioActionFlags,
                                  const AudioTimeStamp* inTimeStamp,
                                  UInt32 inBusNumber,
                                  UInt32 inNumberFrames,
                                  AudioBufferList* ioData)
    {
        Q_UNUSED(inBusNumber);
        Q_UNUSED(ioData);

        auto* self = static_cast<MacAudioInputCaptureBackend*>(inRefCon);
        if (!self || !self->m_audioUnit || !self->m_running.load()) {
            return noErr;
        }

        const int frameCount = static_cast<int>(inNumberFrames);
        const int totalSamples = frameCount * self->m_numChannels;
        if (static_cast<int>(self->m_interleavedBuffer.size()) < totalSamples ||
            static_cast<int>(self->m_captureBufferL.size()) < frameCount ||
            static_cast<int>(self->m_captureBufferR.size()) < frameCount) {
            return noErr;
        }

        AudioBufferList bufferList {};
        bufferList.mNumberBuffers = 1;
        bufferList.mBuffers[0].mNumberChannels = static_cast<UInt32>(self->m_numChannels);
        bufferList.mBuffers[0].mDataByteSize = static_cast<UInt32>(totalSamples * sizeof(float));
        bufferList.mBuffers[0].mData = self->m_interleavedBuffer.data();

        const OSStatus status = AudioUnitRender(self->m_audioUnit,
                                                ioActionFlags,
                                                inTimeStamp,
                                                1,
                                                inNumberFrames,
                                                &bufferList);
        if (status != noErr) {
            return status;
        }

        for (int frame = 0; frame < frameCount; ++frame) {
            const float left = self->m_interleavedBuffer[frame * self->m_numChannels];
            const float right = (self->m_numChannels >= 2)
                ? self->m_interleavedBuffer[frame * self->m_numChannels + 1]
                : left;
            self->m_captureBufferL[frame] = left;
            self->m_captureBufferR[frame] = right;
        }

        if (self->m_context.captureInvoker && frameCount > 0) {
            self->m_context.captureInvoker(self->m_captureBufferL.data(),
                                           self->m_captureBufferR.data(),
                                           frameCount,
                                           self->m_sampleRate);
        }

        return noErr;
    }

    void cleanup()
    {
        if (m_audioUnit) {
            AudioOutputUnitStop(m_audioUnit);
            AudioUnitUninitialize(m_audioUnit);
            AudioComponentInstanceDispose(m_audioUnit);
            m_audioUnit = nullptr;
        }
        m_inputDeviceId = kAudioObjectUnknown;
        m_interleavedBuffer.clear();
        m_captureBufferL.clear();
        m_captureBufferR.clear();
        m_initialized = false;
        m_running.store(false);
    }

    Context m_context;
    AudioUnit m_audioUnit = nullptr;
    AudioDeviceID m_inputDeviceId = kAudioObjectUnknown;
    std::vector<float> m_interleavedBuffer;
    std::vector<float> m_captureBufferL;
    std::vector<float> m_captureBufferR;
    std::atomic<bool> m_running{false};
    double m_sampleRate = 44100.0;
    int m_numChannels = 2;
    int m_bufferSize = 0;
    bool m_initialized = false;
};
#endif

} // namespace

std::unique_ptr<AudioInputCaptureBackend> createAudioInputCaptureBackend(AudioInputCaptureBackend::Context context)
{
#ifdef Q_OS_WIN
    return std::make_unique<WasapiAudioInputCaptureBackend>(std::move(context));
#elif defined(Q_OS_MAC)
    return std::make_unique<MacAudioInputCaptureBackend>(std::move(context));
#else
    return std::make_unique<UnsupportedAudioInputCaptureBackend>(std::move(context));
#endif
}
