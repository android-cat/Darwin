#include "MidiInputDevice.h"

#include <QDebug>
#include <QMutexLocker>

MidiInputDevice::MidiInputDevice(QObject* parent)
    : QObject(parent)
{
}

MidiInputDevice::~MidiInputDevice()
{
    stop();
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
    }

    if (!recognized) {
        // ライブモニター/録音で必要なのは現状 NoteOn/NoteOff のみ。
        // CC などは今後必要になったらここで拡張する。
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
#endif
