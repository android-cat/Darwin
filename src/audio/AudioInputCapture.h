#pragma once

#include <QObject>
#include <QMutex>
#include <QThread>
#include <atomic>
#include <functional>
#include <vector>

#ifdef Q_OS_WIN
struct IMMDeviceEnumerator;
struct IMMDevice;
struct IAudioClient;
struct IAudioCaptureClient;
typedef void* HANDLE;
#endif

class AudioInputCapture : public QObject
{
    Q_OBJECT

public:
    using CaptureCallback = std::function<void(const float* left,
                                               const float* right,
                                               int numFrames,
                                               double sampleRate)>;

    explicit AudioInputCapture(QObject* parent = nullptr);
    ~AudioInputCapture() override;

    bool initialize();
    bool start();
    void stop();

    bool isRunning() const { return m_running.load(); }
    double sampleRate() const { return m_sampleRate; }
    void setCaptureCallback(CaptureCallback callback);

signals:
    void errorOccurred(const QString& message);

private:
    void captureThread();
    void cleanup();
    void convertCaptureBuffer(const unsigned char* data, int numFrames,
                              std::vector<float>& outL,
                              std::vector<float>& outR) const;

    CaptureCallback m_captureCallback;
    QMutex m_mutex;

#ifdef Q_OS_WIN
    IMMDeviceEnumerator* m_enumerator = nullptr;
    IMMDevice* m_device = nullptr;
    IAudioClient* m_audioClient = nullptr;
    IAudioCaptureClient* m_captureClient = nullptr;
    void* m_mixFormat = nullptr;
    HANDLE m_eventHandle = nullptr;
#endif

    double m_sampleRate = 44100.0;
    int m_numChannels = 2;
    int m_bufferSize = 0;

    std::atomic<bool> m_running{false};
    QThread* m_thread = nullptr;
    bool m_initialized = false;
};
