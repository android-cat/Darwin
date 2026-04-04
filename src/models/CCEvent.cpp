#include "CCEvent.h"
#include <QJsonObject>

CCEvent::CCEvent(int ccNumber, qint64 tick, int value, QObject* parent)
    : QObject(parent)
    , m_ccNumber(ccNumber)
    , m_tick(qMax(0LL, tick))
    , m_value(qBound(0, value, maxValueForCC()))
{
}

void CCEvent::setTick(qint64 tick)
{
    if (m_tick != tick) {
        m_tick = qMax(0LL, tick);
        emit changed();
    }
}

void CCEvent::setValue(int value)
{
    const int clamped = qBound(0, value, maxValueForCC());
    if (m_value != clamped) {
        m_value = clamped;
        emit changed();
    }
}

int CCEvent::maxValueForCC() const
{
    if (m_ccNumber == CC_PITCH_BEND) {
        return 16383; // 14bit
    }
    return 127; // 7bit CC / Aftertouch
}

QJsonObject CCEvent::toJson() const
{
    QJsonObject json;
    json["ccNumber"] = m_ccNumber;
    json["tick"] = m_tick;
    json["value"] = m_value;
    return json;
}

CCEvent* CCEvent::fromJson(const QJsonObject& json, QObject* parent)
{
    int ccNumber = json["ccNumber"].toInt(1);
    qint64 tick = static_cast<qint64>(json["tick"].toDouble(0));
    int value = json["value"].toInt(0);
    return new CCEvent(ccNumber, tick, value, parent);
}
