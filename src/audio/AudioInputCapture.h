#pragma once

#include <QObject>
#include <QMutex>
#include <functional>
#include <memory>

class AudioInputCaptureBackend;

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

    bool isRunning() const;
    double sampleRate() const;
    void setCaptureCallback(CaptureCallback callback);

signals:
    void errorOccurred(const QString& message);

private:
    void dispatchCapturedAudio(const float* left, const float* right, int numFrames, double sampleRate);
    void reportError(const QString& message);

    std::unique_ptr<AudioInputCaptureBackend> m_backend;
    CaptureCallback m_captureCallback;
    mutable QMutex m_mutex;
};
