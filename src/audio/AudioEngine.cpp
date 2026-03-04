#include "AudioEngine.h"
#include <QDebug>

#ifdef Q_OS_WIN
// MinGWではGUID定義が自動的にリンクされないため、ここで生成する
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <avrt.h>
#endif

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent)
{
}

AudioEngine::~AudioEngine()
{
    stop();
    cleanup();
}

bool AudioEngine::initialize()
{
#ifdef Q_OS_WIN
    if (m_initialized) return true;

    HRESULT hr;

    // デバイス列挙子を作成
    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                          IID_IMMDeviceEnumerator, reinterpret_cast<void**>(&m_enumerator));
    if (FAILED(hr)) {
        qWarning() << "AudioEngine: デバイス列挙子の作成に失敗:" << hr;
        return false;
    }

    // デフォルト出力デバイスを取得
    hr = m_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_device);
    if (FAILED(hr)) {
        qWarning() << "AudioEngine: デフォルト出力デバイスの取得に失敗:" << hr;
        cleanup();
        return false;
    }

    // IAudioClientを取得
    hr = m_device->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr,
                            reinterpret_cast<void**>(&m_audioClient));
    if (FAILED(hr)) {
        qWarning() << "AudioEngine: IAudioClientのアクティベートに失敗:" << hr;
        cleanup();
        return false;
    }

    // ミックスフォーマットを取得
    WAVEFORMATEX* mixFormat = nullptr;
    hr = m_audioClient->GetMixFormat(&mixFormat);
    if (FAILED(hr)) {
        qWarning() << "AudioEngine: ミックスフォーマットの取得に失敗:" << hr;
        cleanup();
        return false;
    }

    m_sampleRate = mixFormat->nSamplesPerSec;
    m_numChannels = mixFormat->nChannels;

    qDebug() << "AudioEngine: デバイスフォーマット -"
             << "SampleRate:" << m_sampleRate
             << "Channels:" << m_numChannels
             << "BitsPerSample:" << mixFormat->wBitsPerSample;

    // We need float output. WASAPI shared mode typically provides float format
    // via WAVEFORMATEXTENSIBLE. We'll request the mix format as-is.

    // イベント駆動モードでオーディオクライアントを初期化
    // バッファ長: 10ms (100000 * 100ns = 10ms)
    REFERENCE_TIME requestedDuration = 100000; // 10ms

    m_eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_eventHandle) {
        qWarning() << "AudioEngine: イベントハンドルの作成に失敗";
        CoTaskMemFree(mixFormat);
        cleanup();
        return false;
    }

    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        requestedDuration,
        0,
        mixFormat,
        nullptr);

    if (FAILED(hr)) {
        qWarning() << "AudioEngine: オーディオクライアントの初期化に失敗:" << hr;
        CoTaskMemFree(mixFormat);
        cleanup();
        return false;
    }

    CoTaskMemFree(mixFormat);

    // イベントハンドルをセット
    hr = m_audioClient->SetEventHandle(m_eventHandle);
    if (FAILED(hr)) {
        qWarning() << "AudioEngine: イベントハンドルのセットに失敗:" << hr;
        cleanup();
        return false;
    }

    // 実際のバッファサイズを取得
    UINT32 bufferFrames;
    hr = m_audioClient->GetBufferSize(&bufferFrames);
    if (FAILED(hr)) {
        qWarning() << "AudioEngine: バッファサイズの取得に失敗:" << hr;
        cleanup();
        return false;
    }
    m_bufferSize = static_cast<int>(bufferFrames);

    // IAudioRenderClientを取得
    hr = m_audioClient->GetService(IID_IAudioRenderClient,
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
#else
    qWarning() << "AudioEngine: Windows以外は未対応";
    return false;
#endif
}

bool AudioEngine::start()
{
#ifdef Q_OS_WIN
    if (!m_initialized) {
        qWarning() << "AudioEngine: 初期化されていません";
        return false;
    }
    if (m_running.load()) return true;

    // 開始前にバッファをゼロクリア
    BYTE* data;
    HRESULT hr = m_renderClient->GetBuffer(m_bufferSize, &data);
    if (SUCCEEDED(hr)) {
        memset(data, 0, m_bufferSize * m_numChannels * sizeof(float));
        m_renderClient->ReleaseBuffer(m_bufferSize, AUDCLNT_BUFFERFLAGS_SILENT);
    }

    // オーディオストリームを開始
    hr = m_audioClient->Start();
    if (FAILED(hr)) {
        qWarning() << "AudioEngine: オーディオストリームの開始に失敗:" << hr;
        return false;
    }

    m_running.store(true);

    // レンダリングスレッドを開始
    m_thread = QThread::create([this]() { renderThread(); });
    m_thread->start();
    m_thread->setPriority(QThread::TimeCriticalPriority);

    qDebug() << "AudioEngine: レンダリング開始";
    return true;
#else
    return false;
#endif
}

void AudioEngine::stop()
{
#ifdef Q_OS_WIN
    if (!m_running.load()) return;

    m_running.store(false);

    // イベントをシグナルしてスレッドを起こす
    if (m_eventHandle) {
        SetEvent(m_eventHandle);
    }

    if (m_thread) {
        // メインスレッドから呼ばれた場合、2秒のwaitはメッセージループをブロックしてデッドロックの原因になる
        // 短い時間だけ待機し、それでも終わらなければ強制終了またはクリーンアップに移る
        if (!m_thread->wait(100)) {
            qWarning() << "AudioEngine: レンダリングスレッドの終了待機がタイムアウトしました";
            // スレッドが固まっている場合はそのままにしておく（強制終了はクラッシュの元）
        }
        delete m_thread;
        m_thread = nullptr;
    }

    if (m_audioClient) {
        m_audioClient->Stop();
        m_audioClient->Reset();
    }

    qDebug() << "AudioEngine: レンダリング停止";
#endif
}

void AudioEngine::setRenderCallback(RenderCallback callback)
{
    QMutexLocker locker(&m_mutex);
    m_renderCallback = std::move(callback);
}

void AudioEngine::renderThread()
{
#ifdef Q_OS_WIN
    // COMを初期化（レンダリングスレッド用）
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // スレッドのタイマー精度を上げる
    // MMCSS（Multimedia Class Scheduler Service）を使用
    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (!hTask) {
        qWarning() << "AudioEngine: MMCSSの設定に失敗";
    }

    std::vector<float> tempBuffer;

    while (m_running.load()) {
        // イベント待機（最大100ms）
        DWORD waitResult = WaitForSingleObject(m_eventHandle, 100);
        if (!m_running.load()) break;
        if (waitResult != WAIT_OBJECT_0) continue;

        // 利用可能なバッファスペースを計算
        UINT32 padding = 0;
        HRESULT hr = m_audioClient->GetCurrentPadding(&padding);
        if (FAILED(hr)) continue;

        UINT32 availableFrames = m_bufferSize - padding;
        if (availableFrames == 0) continue;

        // バッファを取得
        BYTE* data = nullptr;
        hr = m_renderClient->GetBuffer(availableFrames, &data);
        if (FAILED(hr)) continue;

        // tempBufferをリサイズ
        int totalSamples = static_cast<int>(availableFrames) * m_numChannels;
        if (static_cast<int>(tempBuffer.size()) < totalSamples) {
            tempBuffer.resize(totalSamples);
        }

        // ゼロクリア
        memset(tempBuffer.data(), 0, totalSamples * sizeof(float));

        // コールバックでバッファを充填
        {
            QMutexLocker locker(&m_mutex);
            if (m_renderCallback) {
                m_renderCallback(tempBuffer.data(),
                                 static_cast<int>(availableFrames),
                                 m_numChannels,
                                 m_sampleRate);
            }
        }

        // WASAPIバッファにコピー（float → デバイスフォーマット）
        // WASAPI共有モードはfloat32を使用するケースがほとんど
        memcpy(data, tempBuffer.data(), totalSamples * sizeof(float));

        hr = m_renderClient->ReleaseBuffer(availableFrames, 0);
        if (FAILED(hr)) {
            qWarning() << "AudioEngine: ReleaseBuffer失敗:" << hr;
        }
    }

    if (hTask) {
        AvRevertMmThreadCharacteristics(hTask);
    }

    CoUninitialize();
#endif
}

void AudioEngine::cleanup()
{
#ifdef Q_OS_WIN
    if (m_renderClient) { m_renderClient->Release(); m_renderClient = nullptr; }
    if (m_audioClient) { m_audioClient->Release(); m_audioClient = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
    if (m_enumerator) { m_enumerator->Release(); m_enumerator = nullptr; }
    if (m_eventHandle) { CloseHandle(m_eventHandle); m_eventHandle = nullptr; }
    m_initialized = false;
#endif
}
