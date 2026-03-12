#include "WavWriter.h"

#include <QFile>
#include <QtEndian>
#include <algorithm>
#include <cstdint>

namespace {
template <typename T>
void appendValue(QByteArray& buffer, T value)
{
    const T littleEndian = qToLittleEndian(value);
    buffer.append(reinterpret_cast<const char*>(&littleEndian), sizeof(T));
}
}

bool WavWriter::writeStereo16(const QString& filePath,
                              const QVector<float>& samplesL,
                              const QVector<float>& samplesR,
                              int sampleRate,
                              QString* errorMessage)
{
    if (samplesL.size() != samplesR.size()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("L/R チャンネルのサンプル数が一致しません");
        }
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    const int numChannels = 2;
    const int bitsPerSample = 16;
    const int bytesPerSample = bitsPerSample / 8;
    const int numFrames = samplesL.size();
    const uint32_t dataChunkSize = static_cast<uint32_t>(numFrames * numChannels * bytesPerSample);
    const uint32_t riffChunkSize = 36u + dataChunkSize;
    const uint32_t byteRate = static_cast<uint32_t>(sampleRate * numChannels * bytesPerSample);
    const uint16_t blockAlign = static_cast<uint16_t>(numChannels * bytesPerSample);

    // 録音テイク保存用の最小構成WAVヘッダを自前で組み立てる。
    // 依存を増やさず、Clip が参照できるファイルを即座に残すのが目的。
    QByteArray header;
    header.reserve(44);
    header.append("RIFF", 4);
    appendValue<uint32_t>(header, riffChunkSize);
    header.append("WAVE", 4);
    header.append("fmt ", 4);
    appendValue<uint32_t>(header, 16u);
    appendValue<uint16_t>(header, 1u);
    appendValue<uint16_t>(header, static_cast<uint16_t>(numChannels));
    appendValue<uint32_t>(header, static_cast<uint32_t>(sampleRate));
    appendValue<uint32_t>(header, byteRate);
    appendValue<uint16_t>(header, blockAlign);
    appendValue<uint16_t>(header, static_cast<uint16_t>(bitsPerSample));
    header.append("data", 4);
    appendValue<uint32_t>(header, dataChunkSize);

    if (file.write(header) != header.size()) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    QByteArray pcm;
    pcm.resize(static_cast<int>(dataChunkSize));
    auto* dst = reinterpret_cast<int16_t*>(pcm.data());

    for (int i = 0; i < numFrames; ++i) {
        // 内部は float(-1.0〜1.0) なので、保存時だけ 16bit PCM へ量子化する。
        const float l = std::clamp(samplesL.at(i), -1.0f, 1.0f);
        const float r = std::clamp(samplesR.at(i), -1.0f, 1.0f);
        dst[i * 2 + 0] = static_cast<int16_t>(l * 32767.0f);
        dst[i * 2 + 1] = static_cast<int16_t>(r * 32767.0f);
    }

    if (file.write(pcm) != pcm.size()) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    return true;
}
