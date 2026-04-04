#pragma once

#include <QString>
#include <functional>
#include <memory>

class AudioEngineBackend
{
public:
    struct Context {
        std::function<void(float* outputBuffer, int numFrames, int numChannels, double sampleRate)> renderInvoker;
        std::function<void(const QString& message)> errorReporter;
    };

    virtual ~AudioEngineBackend() = default;

    virtual bool initialize() = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;

    virtual bool isRunning() const = 0;
    virtual double sampleRate() const = 0;
    virtual int numChannels() const = 0;
    virtual int bufferSize() const = 0;
};

std::unique_ptr<AudioEngineBackend> createAudioEngineBackend(AudioEngineBackend::Context context);
