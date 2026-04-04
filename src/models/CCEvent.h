#pragma once

#include <QObject>
#include <QJsonObject>

/**
 * @brief MIDI CCオートメーションのブレークポイント（単一の制御点）を表すクラス
 *
 * ピアノロール下部のエクスプレッションレーンで編集する連続コントロール値を保持する。
 * 対応CC: CC1（モジュレーション）、CC11（エクスプレッション）、Pitch Bend、Aftertouch 等。
 */
class CCEvent : public QObject
{
    Q_OBJECT

public:
    /**
     * @param ccNumber  MIDI CC番号（0-127）。特殊値: 128=Pitch Bend, 129=Channel Aftertouch
     * @param tick      クリップ先頭からの相対ティック位置
     * @param value     0-127（CC）、0-16383（Pitch Bend, 中央=8192）、0-127（Aftertouch）
     */
    explicit CCEvent(int ccNumber, qint64 tick, int value, QObject* parent = nullptr);
    ~CCEvent() override = default;

    // ───── 特殊CC番号の定数 ─────
    static constexpr int CC_PITCH_BEND       = 128;
    static constexpr int CC_CHANNEL_PRESSURE = 129;

    // Getters
    int ccNumber() const { return m_ccNumber; }
    qint64 tick() const { return m_tick; }
    int value() const { return m_value; }

    // Setters
    void setTick(qint64 tick);
    void setValue(int value);

    // シリアライズ
    QJsonObject toJson() const;
    static CCEvent* fromJson(const QJsonObject& json, QObject* parent = nullptr);

signals:
    void changed();

private:
    int maxValueForCC() const;

    int m_ccNumber;    ///< CC番号 (0-127, 128=PitchBend, 129=Aftertouch)
    qint64 m_tick;     ///< クリップ先頭からの相対ティック位置
    int m_value;       ///< 値
};
