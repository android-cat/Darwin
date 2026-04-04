#include "AudioEngine.h"
#include "AudioEngineBackend.h"

#include <QMutexLocker>

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent)
    , m_backend(createAudioEngineBackend({
          [this](float* outputBuffer, int numFrames, int numChannels, double sampleRate) {
              invokeRenderCallback(outputBuffer, numFrames, numChannels, sampleRate);
          },
          [this](const QString& message) {
              reportError(message);
          }
      }))
{
}

AudioEngine::~AudioEngine()
{
    stop();
}

bool AudioEngine::initialize()
{
    return m_backend ? m_backend->initialize() : false;
}

bool AudioEngine::start()
{
    return m_backend ? m_backend->start() : false;
}

void AudioEngine::stop()
{
    if (m_backend) {
        m_backend->stop();
    }
}

bool AudioEngine::isRunning() const
{
    return m_backend ? m_backend->isRunning() : false;
}

double AudioEngine::sampleRate() const
{
    return m_backend ? m_backend->sampleRate() : 44100.0;
}

int AudioEngine::numChannels() const
{
    return m_backend ? m_backend->numChannels() : 2;
}

int AudioEngine::bufferSize() const
{
    return m_backend ? m_backend->bufferSize() : 0;
}

void AudioEngine::setRenderCallback(RenderCallback callback)
{
    QMutexLocker locker(&m_mutex);
    m_renderCallback = std::move(callback);
}

void AudioEngine::invokeRenderCallback(float* outputBuffer, int numFrames, int numChannels, double sampleRate)
{
    QMutexLocker locker(&m_mutex);
    if (m_renderCallback) {
        m_renderCallback(outputBuffer, numFrames, numChannels, sampleRate);
    }
}

void AudioEngine::reportError(const QString& message)
{
    emit errorOccurred(message);
}
