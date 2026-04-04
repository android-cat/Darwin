#include "AudioInputCapture.h"
#include "AudioInputCaptureBackend.h"

#include <QMutexLocker>

AudioInputCapture::AudioInputCapture(QObject* parent)
    : QObject(parent)
    , m_backend(createAudioInputCaptureBackend({
          [this](const float* left, const float* right, int numFrames, double sampleRate) {
              dispatchCapturedAudio(left, right, numFrames, sampleRate);
          },
          [this](const QString& message) {
              reportError(message);
          }
      }))
{
}

AudioInputCapture::~AudioInputCapture()
{
    stop();
}

bool AudioInputCapture::initialize()
{
    return m_backend ? m_backend->initialize() : false;
}

bool AudioInputCapture::start()
{
    return m_backend ? m_backend->start() : false;
}

void AudioInputCapture::stop()
{
    if (m_backend) {
        m_backend->stop();
    }
}

bool AudioInputCapture::isRunning() const
{
    return m_backend ? m_backend->isRunning() : false;
}

double AudioInputCapture::sampleRate() const
{
    return m_backend ? m_backend->sampleRate() : 44100.0;
}

void AudioInputCapture::setCaptureCallback(CaptureCallback callback)
{
    QMutexLocker locker(&m_mutex);
    m_captureCallback = std::move(callback);
}

void AudioInputCapture::dispatchCapturedAudio(const float* left, const float* right, int numFrames, double sampleRate)
{
    QMutexLocker locker(&m_mutex);
    if (m_captureCallback) {
        m_captureCallback(left, right, numFrames, sampleRate);
    }
}

void AudioInputCapture::reportError(const QString& message)
{
    emit errorOccurred(message);
}
