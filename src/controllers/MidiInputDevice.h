#pragma once

#include <QObject>
#include <QMutex>
#include <atomic>
#include <functional>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mmsystem.h>
#endif

class MidiInputDevice : public QObject
{
    Q_OBJECT

public:
    struct Message {
        uint8_t type = 0; // 0=NoteOn, 1=NoteOff
        int pitch = 0;
        int velocity = 0;
    };

    using MessageCallback = std::function<void(const Message&)>;

    explicit MidiInputDevice(QObject* parent = nullptr);
    ~MidiInputDevice() override;

    bool initialize();
    bool start();
    void stop();

    bool isRunning() const { return m_running.load(); }
    void setMessageCallback(MessageCallback callback);

signals:
    void errorOccurred(const QString& message);

private:
#ifdef Q_OS_WIN
    static void CALLBACK midiInProc(HMIDIIN handle, UINT msg,
                                    DWORD_PTR instance,
                                    DWORD_PTR param1,
                                    DWORD_PTR param2);
    void handleShortMessage(DWORD_PTR param1);

    HMIDIIN m_handle = nullptr;
    UINT m_deviceId = 0;
#endif

    MessageCallback m_callback;
    QMutex m_mutex;
    std::atomic<bool> m_running{false};
    bool m_initialized = false;
};
