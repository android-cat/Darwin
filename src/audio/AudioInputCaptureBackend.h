#pragma once

#include <QString>
#include <functional>
#include <memory>

class AudioInputCaptureBackend
{
public:
    struct Context {
        std::function<void(const float* left, const float* right, int numFrames, double sampleRate)> captureInvoker;
        std::function<void(const QString& message)> errorReporter;
    };

    virtual ~AudioInputCaptureBackend() = default;

    virtual bool initialize() = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;

    virtual bool isRunning() const = 0;
    virtual double sampleRate() const = 0;
};

std::unique_ptr<AudioInputCaptureBackend> createAudioInputCaptureBackend(AudioInputCaptureBackend::Context context);
