#pragma once
#include <QWidget>
#include <QTimer>
#include <QMap>
#include <QPointer>

class Clip;
class Note;

class VelocityLaneWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VelocityLaneWidget(QWidget *parent = nullptr);
    QSize sizeHint() const override;

public slots:
    void setActiveClip(Clip* clip);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void updateVelocityFromMouse(const QPoint& pos);
    void tickAnimation();

    QPointer<Clip> m_activeClip;
    QPointer<Note> m_interactingNote;
    bool m_isDragging;

    // ベロシティバーの表示用補間値（スムーズアニメーション）
    QMap<Note*, float> m_displayVelocities; // Note* → 表示用ratio (0.0-1.0)
    QTimer m_animTimer;
};
