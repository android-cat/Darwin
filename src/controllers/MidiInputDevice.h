#pragma once

#include <QObject>
#include <QMutex>
#include <QVector>
#include <atomic>
#include <cstdint>
#include <functional>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mmsystem.h>
#elif defined(Q_OS_MAC)
#include <CoreMIDI/CoreMIDI.h>
#endif

class MidiInputDevice : public QObject
{
    Q_OBJECT

public:
    struct Message {
        uint8_t type = 0; // 0=NoteOn, 1=NoteOff, 2=CC, 3=PitchBend, 4=ChannelPressure
        int pitch = 0;
        int velocity = 0;
        uint8_t ccNumber = 0;   ///< CC番号 (type==2)
        uint8_t ccValue  = 0;   ///< CC値 (type==2)
        int16_t bendValue = 8192; ///< Pitch Bend (type==3, 0-16383, 中央=8192)
        uint8_t pressure = 0;   ///< Channel Aftertouch (type==4)
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
#elif defined(Q_OS_MAC)
    static void midiReadProc(const MIDIPacketList* packetList,
                             void* readProcRefCon,
                             void* srcConnRefCon);
    void handlePacketList(const MIDIPacketList* packetList);

    MIDIClientRef m_client = 0;
    MIDIPortRef m_inputPort = 0;
    QVector<MIDIEndpointRef> m_sources;
#endif

    MessageCallback m_callback;
    QMutex m_mutex;
    std::atomic<bool> m_running{false};
    bool m_initialized = false;
};
