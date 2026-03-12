#include "AudioInputCapture.h"

#include <QDebug>
#include <QMutexLocker>
#include <algorithm>
#include <cstdint>
#include <cstring>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#endif

AudioInputCapture::AudioInputCapture(QObject* parent)
    : QObject(parent)
{
}

AudioInputCapture::~AudioInputCapture()
{
    stop();
    cleanup();
}

bool AudioInputCapture::initialize()
{
#ifdef Q_OS_WIN
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
    m_sampleRate = mixFormat->nSamplesPerSec;
    m_numChannels = std::max<int>(1, mixFormat->nChannels);

    m_eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_eventHandle) {
        qWarning() << "AudioInputCapture: イベントハンドルの作成に失敗";
        cleanup();
        return false;
    }

    const REFERENCE_TIME requestedDuration = 100000; // 10ms
    // 共有モード + イベント駆動で OS の既定入力を取り込む。
    // ここでは「録音用バッファへ安定して流し込めること」を優先する。
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
             << "BufferSize:" << m_bufferSize;
    return true;
#else
    qWarning() << "AudioInputCapture: Windows 以外は未対応";
    return false;
#endif
}

bool AudioInputCapture::start()
{
#ifdef Q_OS_WIN
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
#else
    return false;
#endif
}

void AudioInputCapture::stop()
{
#ifdef Q_OS_WIN
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
#endif
}

void AudioInputCapture::setCaptureCallback(CaptureCallback callback)
{
    QMutexLocker locker(&m_mutex);
    m_captureCallback = std::move(callback);
}

void AudioInputCapture::captureThread()
{
#ifdef Q_OS_WIN
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
                // WASAPI の実フォーマットはデバイス依存なので、
                // ここで必ず内部用の L/R float 配列へ正規化してから上流へ渡す。
                convertCaptureBuffer(reinterpret_cast<const unsigned char*>(data),
                                     static_cast<int>(numFrames),
                                     tempLeft, tempRight);
            }

            CaptureCallback callback;
            {
                QMutexLocker locker(&m_mutex);
                callback = m_captureCallback;
            }
            if (callback && numFrames > 0) {
                callback(tempLeft.data(), tempRight.data(),
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
#endif
}

void AudioInputCapture::convertCaptureBuffer(const unsigned char* data, int numFrames,
                                             std::vector<float>& outL,
                                             std::vector<float>& outR) const
{
#ifdef Q_OS_WIN
    if (!m_mixFormat || !data || numFrames <= 0) {
        return;
    }

    const auto* mixFormat = reinterpret_cast<const WAVEFORMATEX*>(m_mixFormat);
    const int channels = std::max<int>(1, static_cast<int>(mixFormat->nChannels));
    const bool isExtensible = (mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE);
    const auto* extensible = isExtensible
        ? reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mixFormat)
        : nullptr;

    const bool isFloat =
        mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (extensible && extensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    const bool isPcm =
        mixFormat->wFormatTag == WAVE_FORMAT_PCM ||
        (extensible && extensible->SubFormat == KSDATAFORMAT_SUBTYPE_PCM);

    const int bitsPerSample = mixFormat->wBitsPerSample;

    auto read24Bit = [](const unsigned char* src) -> float {
        int32_t value = (static_cast<int32_t>(src[0]) |
                         (static_cast<int32_t>(src[1]) << 8) |
                         (static_cast<int32_t>(src[2]) << 16));
        if ((value & 0x800000) != 0) {
            value |= ~0xFFFFFF;
        }
        return static_cast<float>(value / 8388608.0);
    };

    for (int frame = 0; frame < numFrames; ++frame) {
        float left = 0.0f;
        float right = 0.0f;

        if (isFloat && bitsPerSample == 32) {
            const auto* src = reinterpret_cast<const float*>(data);
            left = src[frame * channels];
            right = src[frame * channels + std::min(1, channels - 1)];
        } else if (isPcm && bitsPerSample == 16) {
            const auto* src = reinterpret_cast<const int16_t*>(data);
            left = static_cast<float>(src[frame * channels] / 32768.0f);
            right = static_cast<float>(src[frame * channels + std::min(1, channels - 1)] / 32768.0f);
        } else if (isPcm && bitsPerSample == 24) {
            const int stride = channels * 3;
            const unsigned char* framePtr = data + frame * stride;
            left = read24Bit(framePtr);
            right = read24Bit(framePtr + (std::min(1, channels - 1) * 3));
        } else if (isPcm && bitsPerSample == 32) {
            const auto* src = reinterpret_cast<const int32_t*>(data);
            left = static_cast<float>(src[frame * channels] / 2147483648.0);
            right = static_cast<float>(src[frame * channels + std::min(1, channels - 1)] / 2147483648.0);
        }

        if (channels == 1) {
            right = left;
        }

        outL[frame] = left;
        outR[frame] = right;
    }
#else
    Q_UNUSED(data);
    Q_UNUSED(numFrames);
    Q_UNUSED(outL);
    Q_UNUSED(outR);
#endif
}

void AudioInputCapture::cleanup()
{
#ifdef Q_OS_WIN
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
    m_initialized = false;
#endif
}
