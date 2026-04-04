#pragma once

#include <QWidget>
#include <QTimer>
#include <QPointer>

class Clip;
class CCEvent;

/**
 * @brief エクスプレッションレーンウィジェット
 *
 * ピアノロール下部に表示し、MIDI CC（Expression / Modulation / Pitch Bend / Aftertouch）
 * のブレークポイントカーブを描画・編集する。
 * VelocityLaneWidgetと同じ横スクロール同期の仕組みに乗る。
 */
class ExpressionLaneWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ExpressionLaneWidget(QWidget *parent = nullptr);
    QSize sizeHint() const override;

    /** 表示・編集対象のCC番号を設定 */
    void setCCNumber(int ccNumber);
    int ccNumber() const { return m_ccNumber; }

public slots:
    void setActiveClip(Clip* clip);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    /** マウスY座標 → CC値 */
    int valueFromY(int y) const;
    /** CC値 → 描画Y座標 */
    int yFromValue(int value) const;
    /** マウスX座標 → ティック */
    qint64 tickFromX(int x) const;
    /** ティック → 描画X座標 */
    int xFromTick(qint64 tick) const;

    /** 最大値（CC=127, PitchBend=16383） */
    int maxValue() const;
    /** 中央値（Pitch Bend用：8192、それ以外：-1=使用しない） */
    int centerValue() const;

    /** 近くのCCイベントを検索 */
    CCEvent* findNearEvent(const QPoint& pos, int tolerance = 8) const;

    QPointer<Clip> m_activeClip;
    int m_ccNumber = 11;  ///< デフォルト: CC11 (Expression)

    // インタラクション状態
    QPointer<CCEvent> m_dragEvent;
    bool m_isDragging = false;
};
