#include "AudioEngineBackend.h"

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
#include <Functiondiscoverykeys_devpkey.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#elif defined(Q_OS_MAC)
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>
#endif

#ifdef Q_OS_MAC
namespace {
AudioDeviceID defaultOutputDevice()
{
    AudioDeviceID deviceId = kAudioObjectUnknown;
    UInt32 size = sizeof(deviceId);
    AudioObjectPropertyAddress address {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    const OSStatus status = AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                                       &address,
                                                       0,
                                                       nullptr,
                                                       &size,
                                                       &deviceId);
    return status == noErr ? deviceId : kAudioObjectUnknown;
}

double deviceSampleRate(AudioDeviceID deviceId)
{
    if (deviceId == kAudioObjectUnknown) {
        return 44100.0;
    }

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
    if (deviceId == kAudioObjectUnknown) {
        return 512;
    }

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

QString macAudioError(OSStatus status)
{
    return QStringLiteral("0x%1")
        .arg(static_cast<quint32>(status), 8, 16, QChar('0'));
}
}
#endif

namespace {

void reportBackendError(const AudioEngineBackend::Context& context, const QString& message)
{
    if (context.errorReporter) {
        context.errorReporter(message);
    }
}

#ifdef Q_OS_WIN
struct WasapiSampleFormatInfo {
    bool supported = false;
    bool isFloat = false;
    bool isUnsignedPcm = false;
    int bytesPerSample = 0;
    int validBitsPerSample = 0;
    int blockAlign = 0;
};

WasapiSampleFormatInfo describeWasapiSampleFormat(const WAVEFORMATEX* format)
{
    WasapiSampleFormatInfo info;
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

std::int32_t quantizeSignedPcmSample(float sample, int validBitsPerSample)
{
    if (validBitsPerSample <= 1 || validBitsPerSample > 32) {
        return 0;
    }

    const std::int64_t negativeScale = std::int64_t{1} << (validBitsPerSample - 1);
    const std::int64_t positiveScale = negativeScale - 1;
    const double clamped = std::clamp(static_cast<double>(sample), -1.0, 1.0);
    const double scaled = clamped >= 0.0
        ? clamped * static_cast<double>(positiveScale)
        : clamped * static_cast<double>(negativeScale);
    const std::int64_t quantized = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(std::llround(scaled)),
        -negativeScale,
        positiveScale);
    return static_cast<std::int32_t>(quantized);
}

void writePcmSample(unsigned char* dst,
                    int bytesPerSample,
                    int validBitsPerSample,
                    bool isUnsignedPcm,
                    float sample)
{
    if (!dst || bytesPerSample <= 0) {
        return;
    }

    if (isUnsignedPcm) {
        const std::int32_t signedSample = quantizeSignedPcmSample(sample, 8);
        dst[0] = static_cast<unsigned char>(std::clamp(signedSample + 128, 0, 255));
        return;
    }

    const int containerBits = bytesPerSample * 8;
    const int shift = std::max(0, containerBits - validBitsPerSample);
    const std::int64_t storedValue =
        static_cast<std::int64_t>(quantizeSignedPcmSample(sample, validBitsPerSample)) << shift;
    const std::uint64_t rawValue = static_cast<std::uint64_t>(storedValue);
    for (int byteIndex = 0; byteIndex < bytesPerSample; ++byteIndex) {
        dst[byteIndex] = static_cast<unsigned char>((rawValue >> (byteIndex * 8)) & 0xFFu);
    }
}
#endif

class UnsupportedAudioEngineBackend final : public AudioEngineBackend
{
public:
    explicit UnsupportedAudioEngineBackend(Context context)
        : m_context(std::move(context))
    {
    }

    bool initialize() override
    {
        const QString message = QStringLiteral("AudioEngine: このOS向けのオーディオバックエンドは未実装です");
        qWarning() << message;
        reportBackendError(m_context, message);
        return false;
    }

    bool start() override { return false; }
    void stop() override {}
    bool isRunning() const override { return false; }
    double sampleRate() const override { return 44100.0; }
    int numChannels() const override { return 2; }
    int bufferSize() const override { return 0; }

private:
    Context m_context;
};

#ifdef Q_OS_WIN
class WasapiAudioEngineBackend final : public AudioEngineBackend
{
public:
    explicit WasapiAudioEngineBackend(Context context)
        : m_context(std::move(context))
    {
    }

    ~WasapiAudioEngineBackend() override
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
            qWarning() << "AudioEngine: デバイス列挙子の作成に失敗:" << hr;
            return false;
        }

        hr = m_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_device);
        if (FAILED(hr)) {
            qWarning() << "AudioEngine: デフォルト出力デバイスの取得に失敗:" << hr;
            cleanup();
            return false;
        }

        hr = m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(&m_audioClient));
        if (FAILED(hr)) {
            qWarning() << "AudioEngine: IAudioClientのアクティベートに失敗:" << hr;
            cleanup();
            return false;
        }

        hr = m_audioClient->GetMixFormat(&m_mixFormat);
        if (FAILED(hr) || !m_mixFormat) {
            qWarning() << "AudioEngine: ミックスフォーマットの取得に失敗:" << hr;
            cleanup();
            return false;
        }

        const WasapiSampleFormatInfo sampleFormat = describeWasapiSampleFormat(m_mixFormat);
        if (!sampleFormat.supported) {
            const QString message = QStringLiteral(
                "AudioEngine: この出力デバイスの共有モード形式には未対応です");
            qWarning() << message
                       << "BitsPerSample:" << m_mixFormat->wBitsPerSample
                       << "BlockAlign:" << m_mixFormat->nBlockAlign
                       << "FormatTag:" << m_mixFormat->wFormatTag;
            reportBackendError(m_context, message);
            cleanup();
            return false;
        }

        m_sampleRate = m_mixFormat->nSamplesPerSec;
        m_numChannels = m_mixFormat->nChannels;
        m_mixFormatIsFloat = sampleFormat.isFloat;
        m_mixFormatIsUnsignedPcm = sampleFormat.isUnsignedPcm;
        m_mixFormatBytesPerSample = sampleFormat.bytesPerSample;
        m_mixFormatValidBitsPerSample = sampleFormat.validBitsPerSample;
        m_mixFormatBlockAlign = sampleFormat.blockAlign;

        qDebug() << "AudioEngine: デバイスフォーマット -"
                 << "SampleRate:" << m_sampleRate
                 << "Channels:" << m_numChannels
                 << "BitsPerSample:" << m_mixFormat->wBitsPerSample
                 << "ValidBitsPerSample:" << m_mixFormatValidBitsPerSample
                 << "BytesPerSample:" << m_mixFormatBytesPerSample
                 << "Float:" << m_mixFormatIsFloat;

        const REFERENCE_TIME requestedDuration = 100000; // 10ms

        m_eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!m_eventHandle) {
            qWarning() << "AudioEngine: イベントハンドルの作成に失敗";
            cleanup();
            return false;
        }

        hr = m_audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            requestedDuration,
            0,
            m_mixFormat,
            nullptr);

        if (FAILED(hr)) {
            qWarning() << "AudioEngine: オーディオクライアントの初期化に失敗:" << hr;
            cleanup();
            return false;
        }

        hr = m_audioClient->SetEventHandle(m_eventHandle);
        if (FAILED(hr)) {
            qWarning() << "AudioEngine: イベントハンドルのセットに失敗:" << hr;
            cleanup();
            return false;
        }

        UINT32 bufferFrames = 0;
        hr = m_audioClient->GetBufferSize(&bufferFrames);
        if (FAILED(hr)) {
            qWarning() << "AudioEngine: バッファサイズの取得に失敗:" << hr;
            cleanup();
            return false;
        }
        m_bufferSize = static_cast<int>(bufferFrames);

        hr = m_audioClient->GetService(__uuidof(IAudioRenderClient),
                                       reinterpret_cast<void**>(&m_renderClient));
        if (FAILED(hr)) {
            qWarning() << "AudioEngine: IAudioRenderClientの取得に失敗:" << hr;
            cleanup();
            return false;
        }

        m_initialized = true;
        qDebug() << "AudioEngine: 初期化完了 - BufferSize:" << m_bufferSize
                 << "SampleRate:" << m_sampleRate
                 << "Channels:" << m_numChannels;
        return true;
    }

    bool start() override
    {
        if (!m_initialized) {
            qWarning() << "AudioEngine: 初期化されていません";
            return false;
        }
        if (m_running.load()) {
            return true;
        }

        BYTE* data = nullptr;
        HRESULT hr = m_renderClient->GetBuffer(m_bufferSize, &data);
        if (SUCCEEDED(hr)) {
            std::memset(data, 0, static_cast<size_t>(m_bufferSize * m_mixFormatBlockAlign));
            m_renderClient->ReleaseBuffer(m_bufferSize, AUDCLNT_BUFFERFLAGS_SILENT);
        }

        hr = m_audioClient->Start();
        if (FAILED(hr)) {
            qWarning() << "AudioEngine: オーディオストリームの開始に失敗:" << hr;
            return false;
        }

        m_running.store(true);
        m_thread = QThread::create([this]() { renderThread(); });
        m_thread->start();
        m_thread->setPriority(QThread::TimeCriticalPriority);

        qDebug() << "AudioEngine: レンダリング開始";
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
                qWarning() << "AudioEngine: レンダリングスレッドの終了待機がタイムアウトしました";
            }
            delete m_thread;
            m_thread = nullptr;
        }

        if (m_audioClient) {
            m_audioClient->Stop();
            m_audioClient->Reset();
        }

        qDebug() << "AudioEngine: レンダリング停止";
    }

    bool isRunning() const override { return m_running.load(); }
    double sampleRate() const override { return m_sampleRate; }
    int numChannels() const override { return m_numChannels; }
    int bufferSize() const override { return m_bufferSize; }

private:
    void renderThread()
    {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        DWORD taskIndex = 0;
        HANDLE mmcssTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
        if (!mmcssTask) {
            qWarning() << "AudioEngine: MMCSSの設定に失敗";
        }

        std::vector<float> tempBuffer;

        while (m_running.load()) {
            const DWORD waitResult = WaitForSingleObject(m_eventHandle, 100);
            if (!m_running.load()) {
                break;
            }
            if (waitResult != WAIT_OBJECT_0) {
                continue;
            }

            UINT32 padding = 0;
            HRESULT hr = m_audioClient->GetCurrentPadding(&padding);
            if (FAILED(hr)) {
                continue;
            }

            const UINT32 availableFrames = static_cast<UINT32>(m_bufferSize) - padding;
            if (availableFrames == 0) {
                continue;
            }

            BYTE* data = nullptr;
            hr = m_renderClient->GetBuffer(availableFrames, &data);
            if (FAILED(hr)) {
                continue;
            }

            const int totalSamples = static_cast<int>(availableFrames) * m_numChannels;
            if (static_cast<int>(tempBuffer.size()) < totalSamples) {
                tempBuffer.resize(static_cast<size_t>(totalSamples));
            }

            std::memset(tempBuffer.data(), 0, static_cast<size_t>(totalSamples) * sizeof(float));
            if (m_context.renderInvoker) {
                m_context.renderInvoker(tempBuffer.data(),
                                        static_cast<int>(availableFrames),
                                        m_numChannels,
                                        m_sampleRate);
            }

            // WASAPI の共有モードは float 固定ではないため、実デバイス形式へ詰め替える。
            if (m_mixFormatIsFloat && m_mixFormatBytesPerSample == static_cast<int>(sizeof(float))) {
                std::memcpy(data, tempBuffer.data(), static_cast<size_t>(totalSamples) * sizeof(float));
            } else if (m_mixFormatIsFloat &&
                       m_mixFormatBytesPerSample == static_cast<int>(sizeof(double))) {
                auto* dst = reinterpret_cast<double*>(data);
                for (int sampleIndex = 0; sampleIndex < totalSamples; ++sampleIndex) {
                    dst[sampleIndex] = static_cast<double>(tempBuffer[sampleIndex]);
                }
            } else {
                auto* dst = reinterpret_cast<unsigned char*>(data);
                for (int sampleIndex = 0; sampleIndex < totalSamples; ++sampleIndex) {
                    writePcmSample(dst + (sampleIndex * m_mixFormatBytesPerSample),
                                   m_mixFormatBytesPerSample,
                                   m_mixFormatValidBitsPerSample,
                                   m_mixFormatIsUnsignedPcm,
                                   tempBuffer[sampleIndex]);
                }
            }

            hr = m_renderClient->ReleaseBuffer(availableFrames, 0);
            if (FAILED(hr)) {
                qWarning() << "AudioEngine: ReleaseBuffer失敗:" << hr;
            }
        }

        if (mmcssTask) {
            AvRevertMmThreadCharacteristics(mmcssTask);
        }

        CoUninitialize();
    }

    void cleanup()
    {
        if (m_renderClient) {
            m_renderClient->Release();
            m_renderClient = nullptr;
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
        if (m_eventHandle) {
            CloseHandle(m_eventHandle);
            m_eventHandle = nullptr;
        }
        if (m_mixFormat) {
            CoTaskMemFree(m_mixFormat);
            m_mixFormat = nullptr;
        }
        m_mixFormatIsFloat = true;
        m_mixFormatIsUnsignedPcm = false;
        m_mixFormatBytesPerSample = static_cast<int>(sizeof(float));
        m_mixFormatValidBitsPerSample = 32;
        m_mixFormatBlockAlign = 0;
        m_initialized = false;
    }

    Context m_context;
    IMMDeviceEnumerator* m_enumerator = nullptr;
    IMMDevice* m_device = nullptr;
    IAudioClient* m_audioClient = nullptr;
    IAudioRenderClient* m_renderClient = nullptr;
    WAVEFORMATEX* m_mixFormat = nullptr;
    HANDLE m_eventHandle = nullptr;
    QThread* m_thread = nullptr;
    std::atomic<bool> m_running{false};
    double m_sampleRate = 44100.0;
    int m_numChannels = 2;
    int m_bufferSize = 0;
    bool m_mixFormatIsFloat = true;
    bool m_mixFormatIsUnsignedPcm = false;
    int m_mixFormatBytesPerSample = static_cast<int>(sizeof(float));
    int m_mixFormatValidBitsPerSample = 32;
    int m_mixFormatBlockAlign = 0;
    bool m_initialized = false;
};
#endif

#ifdef Q_OS_MAC
class MacAudioEngineBackend final : public AudioEngineBackend
{
public:
    explicit MacAudioEngineBackend(Context context)
        : m_context(std::move(context))
    {
    }

    ~MacAudioEngineBackend() override
    {
        stop();
        cleanup();
    }

    bool initialize() override
    {
        if (m_initialized) {
            return true;
        }

        m_outputDeviceId = defaultOutputDevice();
        if (m_outputDeviceId == kAudioObjectUnknown) {
            qWarning() << "AudioEngine: デフォルト出力デバイスの取得に失敗";
            return false;
        }

        m_sampleRate = deviceSampleRate(m_outputDeviceId);
        m_numChannels = 2;
        m_bufferSize = static_cast<int>(deviceBufferSize(m_outputDeviceId));

        AudioComponentDescription description {};
        description.componentType = kAudioUnitType_Output;
        description.componentSubType = kAudioUnitSubType_DefaultOutput;
        description.componentManufacturer = kAudioUnitManufacturer_Apple;

        AudioComponent component = AudioComponentFindNext(nullptr, &description);
        if (!component) {
            qWarning() << "AudioEngine: DefaultOutput AudioUnit が見つかりません";
            return false;
        }

        OSStatus status = AudioComponentInstanceNew(component, &m_audioUnit);
        if (status != noErr || !m_audioUnit) {
            qWarning() << "AudioEngine: AudioUnit 作成に失敗:" << macAudioError(status);
            cleanup();
            return false;
        }

        AudioStreamBasicDescription streamFormat {};
        streamFormat.mSampleRate = m_sampleRate;
        streamFormat.mFormatID = kAudioFormatLinearPCM;
        streamFormat.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        streamFormat.mBytesPerPacket = sizeof(float);
        streamFormat.mFramesPerPacket = 1;
        streamFormat.mBytesPerFrame = sizeof(float);
        streamFormat.mChannelsPerFrame = static_cast<UInt32>(m_numChannels);
        streamFormat.mBitsPerChannel = 32;

        status = AudioUnitSetProperty(m_audioUnit,
                                      kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Input,
                                      0,
                                      &streamFormat,
                                      sizeof(streamFormat));
        if (status != noErr) {
            qWarning() << "AudioEngine: ストリームフォーマット設定に失敗:" << macAudioError(status);
            cleanup();
            return false;
        }

        AURenderCallbackStruct callback {};
        callback.inputProc = &MacAudioEngineBackend::renderCallback;
        callback.inputProcRefCon = this;
        status = AudioUnitSetProperty(m_audioUnit,
                                      kAudioUnitProperty_SetRenderCallback,
                                      kAudioUnitScope_Input,
                                      0,
                                      &callback,
                                      sizeof(callback));
        if (status != noErr) {
            qWarning() << "AudioEngine: レンダーコールバック設定に失敗:" << macAudioError(status);
            cleanup();
            return false;
        }

        status = AudioUnitInitialize(m_audioUnit);
        if (status != noErr) {
            qWarning() << "AudioEngine: AudioUnit 初期化に失敗:" << macAudioError(status);
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
        m_initialized = true;

        qDebug() << "AudioEngine: macOS初期化完了 - BufferSize:" << m_bufferSize
                 << "SampleRate:" << m_sampleRate
                 << "Channels:" << m_numChannels;
        return true;
    }

    bool start() override
    {
        if (!m_initialized && !initialize()) {
            qWarning() << "AudioEngine: 初期化されていません";
            return false;
        }
        if (m_running.load()) {
            return true;
        }

        const OSStatus status = AudioOutputUnitStart(m_audioUnit);
        if (status != noErr) {
            qWarning() << "AudioEngine: AudioUnit 開始に失敗:" << macAudioError(status);
            return false;
        }

        m_running.store(true);
        qDebug() << "AudioEngine: macOSレンダリング開始";
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
                qWarning() << "AudioEngine: AudioUnit 停止に失敗:" << macAudioError(status);
            }
        }

        qDebug() << "AudioEngine: macOSレンダリング停止";
    }

    bool isRunning() const override { return m_running.load(); }
    double sampleRate() const override { return m_sampleRate; }
    int numChannels() const override { return m_numChannels; }
    int bufferSize() const override { return m_bufferSize; }

private:
    static OSStatus renderCallback(void* inRefCon,
                                   AudioUnitRenderActionFlags* ioActionFlags,
                                   const AudioTimeStamp* inTimeStamp,
                                   UInt32 inBusNumber,
                                   UInt32 inNumberFrames,
                                   AudioBufferList* ioData)
    {
        Q_UNUSED(ioActionFlags);
        Q_UNUSED(inTimeStamp);
        Q_UNUSED(inBusNumber);

        auto* self = static_cast<MacAudioEngineBackend*>(inRefCon);
        if (!self || !ioData || !self->m_running.load()) {
            return noErr;
        }

        const int frameCount = static_cast<int>(inNumberFrames);
        const int totalSamples = frameCount * self->m_numChannels;
        if (static_cast<int>(self->m_interleavedBuffer.size()) < totalSamples) {
            for (UInt32 i = 0; i < ioData->mNumberBuffers; ++i) {
                if (ioData->mBuffers[i].mData) {
                    std::memset(ioData->mBuffers[i].mData, 0, ioData->mBuffers[i].mDataByteSize);
                }
            }
            return noErr;
        }

        std::fill_n(self->m_interleavedBuffer.data(), totalSamples, 0.0f);
        if (self->m_context.renderInvoker) {
            self->m_context.renderInvoker(self->m_interleavedBuffer.data(),
                                          frameCount,
                                          self->m_numChannels,
                                          self->m_sampleRate);
        }

        for (UInt32 bufferIndex = 0; bufferIndex < ioData->mNumberBuffers; ++bufferIndex) {
            AudioBuffer& buffer = ioData->mBuffers[bufferIndex];
            if (!buffer.mData) {
                continue;
            }

            float* destination = static_cast<float*>(buffer.mData);
            if (ioData->mNumberBuffers == 1 && self->m_numChannels >= 2) {
                for (int frame = 0; frame < frameCount; ++frame) {
                    const float left = self->m_interleavedBuffer[frame * self->m_numChannels];
                    const float right = self->m_interleavedBuffer[frame * self->m_numChannels + 1];
                    destination[frame] = (left + right) * 0.5f;
                }
                buffer.mDataByteSize = static_cast<UInt32>(frameCount * sizeof(float));
                continue;
            }

            const int channelIndex = std::min<int>(static_cast<int>(bufferIndex), self->m_numChannels - 1);
            if (channelIndex < 0) {
                std::fill_n(destination, frameCount, 0.0f);
                continue;
            }

            for (int frame = 0; frame < frameCount; ++frame) {
                destination[frame] = self->m_interleavedBuffer[frame * self->m_numChannels + channelIndex];
            }
            buffer.mDataByteSize = static_cast<UInt32>(frameCount * sizeof(float));
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
        m_outputDeviceId = kAudioObjectUnknown;
        m_interleavedBuffer.clear();
        m_initialized = false;
        m_running.store(false);
    }

    Context m_context;
    AudioUnit m_audioUnit = nullptr;
    AudioDeviceID m_outputDeviceId = kAudioObjectUnknown;
    std::vector<float> m_interleavedBuffer;
    std::atomic<bool> m_running{false};
    double m_sampleRate = 44100.0;
    int m_numChannels = 2;
    int m_bufferSize = 0;
    bool m_initialized = false;
};
#endif

} // namespace

std::unique_ptr<AudioEngineBackend> createAudioEngineBackend(AudioEngineBackend::Context context)
{
#ifdef Q_OS_WIN
    return std::make_unique<WasapiAudioEngineBackend>(std::move(context));
#elif defined(Q_OS_MAC)
    return std::make_unique<MacAudioEngineBackend>(std::move(context));
#else
    return std::make_unique<UnsupportedAudioEngineBackend>(std::move(context));
#endif
}
