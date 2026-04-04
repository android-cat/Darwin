#include "MidiInputDevice.h"

#include <QDebug>
#include <QMutexLocker>

namespace {
int midiDataLength(uint8_t status)
{
    const uint8_t command = status & 0xF0;
    if (command == 0xC0 || command == 0xD0) {
        return 1;
    }
    if (command >= 0x80 && command <= 0xE0) {
        return 2;
    }

    switch (status) {
    case 0xF1:
    case 0xF3:
        return 1;
    case 0xF2:
        return 2;
    default:
        return 0;
    }
}
}

MidiInputDevice::MidiInputDevice(QObject* parent)
    : QObject(parent)
{
}

MidiInputDevice::~MidiInputDevice()
{
    stop();
#ifdef Q_OS_MAC
    if (m_inputPort != 0) {
        MIDIPortDispose(m_inputPort);
        m_inputPort = 0;
    }
    if (m_client != 0) {
        MIDIClientDispose(m_client);
        m_client = 0;
    }
#endif
}

bool MidiInputDevice::initialize()
{
#ifdef Q_OS_WIN
    if (m_initialized) {
        return true;
    }

    const UINT numDevices = midiInGetNumDevs();
    if (numDevices == 0) {
        qWarning() << "MidiInputDevice: 利用可能な MIDI 入力デバイスがありません";
        return false;
    }

    // 現状は最初に見つかった入力デバイスをそのまま使う。
    // デバイス選択 UI を入れる場合はここを差し替える。
    m_deviceId = 0;
    m_initialized = true;
    return true;
#elif defined(Q_OS_MAC)
    if (m_initialized) {
        return true;
    }

    OSStatus status = MIDIClientCreate(CFSTR("Darwin MIDI Client"), nullptr, nullptr, &m_client);
    if (status != noErr) {
        qWarning() << "MidiInputDevice: MIDIClientCreate に失敗:" << status;
        m_client = 0;
        return false;
    }

    status = MIDIInputPortCreate(m_client,
                                 CFSTR("Darwin MIDI Input"),
                                 &MidiInputDevice::midiReadProc,
                                 this,
                                 &m_inputPort);
    if (status != noErr) {
        qWarning() << "MidiInputDevice: MIDIInputPortCreate に失敗:" << status;
        MIDIClientDispose(m_client);
        m_client = 0;
        m_inputPort = 0;
        return false;
    }

    const ItemCount numSources = MIDIGetNumberOfSources();
    if (numSources == 0) {
        qWarning() << "MidiInputDevice: 利用可能な MIDI 入力ソースがありません";
        MIDIPortDispose(m_inputPort);
        MIDIClientDispose(m_client);
        m_inputPort = 0;
        m_client = 0;
        return false;
    }

    m_sources.clear();
    for (ItemCount i = 0; i < numSources; ++i) {
        MIDIEndpointRef source = MIDIGetSource(i);
        if (source != 0) {
            m_sources.append(source);
        }
    }

    if (m_sources.isEmpty()) {
        qWarning() << "MidiInputDevice: MIDI ソースの取得に失敗しました";
        MIDIPortDispose(m_inputPort);
        MIDIClientDispose(m_client);
        m_inputPort = 0;
        m_client = 0;
        return false;
    }

    m_initialized = true;
    return true;
#else
    return false;
#endif
}

bool MidiInputDevice::start()
{
#ifdef Q_OS_WIN
    if (!m_initialized && !initialize()) {
        return false;
    }

    if (m_running.load()) {
        return true;
    }

    MMRESULT result = midiInOpen(&m_handle, m_deviceId,
                                 reinterpret_cast<DWORD_PTR>(&MidiInputDevice::midiInProc),
                                 reinterpret_cast<DWORD_PTR>(this),
                                 CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR) {
        qWarning() << "MidiInputDevice: midiInOpen に失敗:" << result;
        m_handle = nullptr;
        return false;
    }

    result = midiInStart(m_handle);
    if (result != MMSYSERR_NOERROR) {
        qWarning() << "MidiInputDevice: midiInStart に失敗:" << result;
        midiInClose(m_handle);
        m_handle = nullptr;
        return false;
    }

    m_running.store(true);
    return true;
#elif defined(Q_OS_MAC)
    if (!m_initialized && !initialize()) {
        return false;
    }

    if (m_running.load()) {
        return true;
    }

    bool connected = false;
    for (MIDIEndpointRef source : m_sources) {
        if (MIDIPortConnectSource(m_inputPort, source, nullptr) == noErr) {
            connected = true;
        }
    }

    if (!connected) {
        qWarning() << "MidiInputDevice: MIDI ソースへ接続できませんでした";
        return false;
    }

    m_running.store(true);
    return true;
#else
    return false;
#endif
}

void MidiInputDevice::stop()
{
#ifdef Q_OS_WIN
    if (!m_handle) {
        m_running.store(false);
        return;
    }

    m_running.store(false);
    midiInStop(m_handle);
    midiInReset(m_handle);
    midiInClose(m_handle);
    m_handle = nullptr;
#elif defined(Q_OS_MAC)
    m_running.store(false);
    if (m_inputPort != 0) {
        for (MIDIEndpointRef source : m_sources) {
            MIDIPortDisconnectSource(m_inputPort, source);
        }
    }
#endif
}

void MidiInputDevice::setMessageCallback(MessageCallback callback)
{
    QMutexLocker locker(&m_mutex);
    m_callback = std::move(callback);
}

#ifdef Q_OS_WIN
void CALLBACK MidiInputDevice::midiInProc(HMIDIIN handle, UINT msg,
                                          DWORD_PTR instance,
                                          DWORD_PTR param1,
                                          DWORD_PTR param2)
{
    Q_UNUSED(handle);
    Q_UNUSED(param2);

    auto* self = reinterpret_cast<MidiInputDevice*>(instance);
    if (!self) {
        return;
    }

    if (msg == MIM_DATA || msg == MM_MIM_DATA) {
        self->handleShortMessage(param1);
    }
}

void MidiInputDevice::handleShortMessage(DWORD_PTR param1)
{
    const uint8_t status = static_cast<uint8_t>(param1 & 0xFF);
    const uint8_t data1 = static_cast<uint8_t>((param1 >> 8) & 0xFF);
    const uint8_t data2 = static_cast<uint8_t>((param1 >> 16) & 0xFF);
    const uint8_t command = status & 0xF0;

    Message message;
    bool recognized = false;

    if (command == 0x90 && data2 > 0) {
        message.type = 0;
        message.pitch = data1;
        message.velocity = data2;
        recognized = true;
    } else if (command == 0x80 || (command == 0x90 && data2 == 0)) {
        message.type = 1;
        message.pitch = data1;
        message.velocity = data2;
        recognized = true;
    } else if (command == 0xB0) {
        // Control Change
        message.type = 2;
        message.ccNumber = data1;
        message.ccValue = data2;
        recognized = true;
    } else if (command == 0xE0) {
        // Pitch Bend（14bit: data1=LSB, data2=MSB）
        message.type = 3;
        message.bendValue = static_cast<int16_t>(data1 | (data2 << 7));
        recognized = true;
    } else if (command == 0xD0) {
        // Channel Aftertouch（1データバイト — data1が値）
        message.type = 4;
        message.pressure = data1;
        recognized = true;
    }

    if (!recognized) {
        return;
    }

    MessageCallback callback;
    {
        QMutexLocker locker(&m_mutex);
        callback = m_callback;
    }
    if (callback) {
        callback(message);
    }
}
#elif defined(Q_OS_MAC)
void MidiInputDevice::midiReadProc(const MIDIPacketList* packetList,
                                   void* readProcRefCon,
                                   void* srcConnRefCon)
{
    Q_UNUSED(srcConnRefCon);

    auto* self = static_cast<MidiInputDevice*>(readProcRefCon);
    if (!self || !packetList || !self->m_running.load()) {
        return;
    }

    self->handlePacketList(packetList);
}

void MidiInputDevice::handlePacketList(const MIDIPacketList* packetList)
{
    const MIDIPacket* packet = &packetList->packet[0];
    for (UInt32 packetIndex = 0; packetIndex < packetList->numPackets; ++packetIndex) {
        uint8_t runningStatus = 0;
        UInt16 offset = 0;

        while (offset < packet->length) {
            uint8_t status = packet->data[offset];
            if ((status & 0x80) != 0) {
                runningStatus = status;
                ++offset;
            } else if (runningStatus != 0) {
                status = runningStatus;
            } else {
                break;
            }

            const int dataLength = midiDataLength(status);
            if (dataLength <= 0 || offset + dataLength > packet->length) {
                break;
            }

            const uint8_t data1 = packet->data[offset];
            const uint8_t data2 = dataLength > 1 ? packet->data[offset + 1] : 0;
            offset += static_cast<UInt16>(dataLength);

            const uint8_t command = status & 0xF0;
            Message message;
            bool recognized = false;

            if (command == 0x90 && data2 > 0) {
                message.type = 0;
                message.pitch = data1;
                message.velocity = data2;
                recognized = true;
            } else if (command == 0x80 || (command == 0x90 && data2 == 0)) {
                message.type = 1;
                message.pitch = data1;
                message.velocity = data2;
                recognized = true;
            } else if (command == 0xB0) {
                // Control Change
                message.type = 2;
                message.ccNumber = data1;
                message.ccValue = data2;
                recognized = true;
            } else if (command == 0xE0) {
                // Pitch Bend（14bit: data1=LSB, data2=MSB）
                message.type = 3;
                message.bendValue = static_cast<int16_t>(data1 | (data2 << 7));
                recognized = true;
            } else if (command == 0xD0) {
                // Channel Aftertouch
                message.type = 4;
                message.pressure = data1;
                recognized = true;
            }

            if (!recognized) {
                continue;
            }

            MessageCallback callback;
            {
                QMutexLocker locker(&m_mutex);
                callback = m_callback;
            }
            if (callback) {
                callback(message);
            }
        }

        packet = MIDIPacketNext(packet);
    }
}
#endif
