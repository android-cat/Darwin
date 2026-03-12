#pragma once

#include <QWidget>

class QPropertyAnimation;

class RecordModeButton : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal slotProgress READ slotProgress WRITE setSlotProgress)

public:
    enum class Mode {
        Audio,
        Midi
    };
    Q_ENUM(Mode)

    explicit RecordModeButton(QWidget* parent = nullptr);

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

    bool isRecording() const { return m_recording; }
    void setRecording(bool recording);

    qreal slotProgress() const { return m_slotProgress; }
    void setSlotProgress(qreal progress);

    QSize sizeHint() const override { return QSize(40, 40); }
    QSize minimumSizeHint() const override { return QSize(40, 40); }

signals:
    void clicked();
    void modeChanged(RecordModeButton::Mode mode);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void animateSlotProgress(qreal target);
    void updatePreviewMode(const QPoint& pos);
    static Mode alternateModeFor(Mode mode);

    Mode m_mode = Mode::Audio;
    Mode m_previewMode = Mode::Audio;
    Mode m_gestureOriginMode = Mode::Audio;
    bool m_recording = false;
    bool m_pressed = false;
    bool m_switchGesture = false;
    QPoint m_pressPos;
    qreal m_slotDirection = -1.0;
    qreal m_slotProgress = 0.0;
    QPropertyAnimation* m_slotAnimation = nullptr;
};
