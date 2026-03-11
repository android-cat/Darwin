#include "FaderWidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <QtMath>
#include "common/Constants.h"
#include "common/ThemeManager.h"

FaderWidget::FaderWidget(QWidget *parent)
    : QWidget(parent)
    , m_value(0.8f) // Default 80%
    , m_displayValue(0.8f)
    , m_isDragging(false)
{
    setMinimumSize(40, 100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // テーマ変更時に再描画
    connect(&Darwin::ThemeManager::instance(), &Darwin::ThemeManager::themeChanged,
            this, [this]() { update(); });

    // スムーズアニメーション用タイマー（60fps）
    connect(&m_animTimer, &QTimer::timeout, this, &FaderWidget::tickAnimation);
    m_animTimer.setInterval(Darwin::UI_ANIMATION_INTERVAL_MS);
}

void FaderWidget::setValue(float value)
{
    float bounded = qBound(0.0f, value, 1.0f);
    if (!qFuzzyCompare(m_value, bounded)) {
        m_value = bounded;
        // ドラッグ中は即座に追従、それ以外はスムーズに補間
        if (m_isDragging) {
            m_displayValue = m_value;
        } else if (!m_animTimer.isActive()) {
            m_animTimer.start();
        }
        update();
        emit valueChanged(m_value);
    }
}

void FaderWidget::tickAnimation()
{
    float diff = m_value - m_displayValue;
    if (qAbs(diff) < 0.002f) {
        m_displayValue = m_value;
        m_animTimer.stop();
    } else {
        // スムーズな補間（lerp factor 0.25 ≈ ~16ms at 60fps）
        m_displayValue += diff * 0.25f;
    }
    update();
}

void FaderWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();
    int margin = 8;
    int trackWidth = 2;
    int trackHeight = h - margin * 2;
    int xCenter = w / 2;

    // Background track
    const Darwin::ThemeManager& tm = Darwin::ThemeManager::instance();
    p.setPen(Qt::NoPen);
    p.setBrush(tm.borderColor());
    p.drawRoundedRect(xCenter - trackWidth / 2, margin, trackWidth, trackHeight, 1, 1);

    // Active track (from bottom up to display value)
    int activeHeight = static_cast<int>(trackHeight * m_displayValue);
    int activeY = margin + trackHeight - activeHeight;
    p.setBrush(tm.accentColor()); // theme accent
    p.drawRoundedRect(xCenter - trackWidth / 2, activeY, trackWidth, activeHeight, 1, 1);

    // Fader Handle
    int handleWidth = 36;
    int handleHeight = 8;
    int handleY = margin + trackHeight - static_cast<int>(trackHeight * m_displayValue) - handleHeight / 2;
    
    handleY = qBound(margin - handleHeight/2, handleY, h - margin - handleHeight/2);

    QRect handleRect(xCenter - handleWidth / 2, handleY, handleWidth, handleHeight);
    
    // ドラッグ中はハンドルを少しハイライト
    if (m_isDragging) {
        p.setPen(QPen(tm.accentColor(), 1.5));
        QColor highlightBg = tm.accentColor();
        highlightBg.setAlpha(60);
        p.setBrush(highlightBg);
    } else {
        p.setPen(QPen(tm.borderColor(), 1));
        p.setBrush(tm.panelBackgroundColor());
    }
    p.drawRoundedRect(handleRect, 2, 2);
}

void FaderWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        int margin = 8;
        int trackHeight = height() - margin * 2;
        int y = qBound(margin, event->pos().y(), height() - margin);
        
        float val = 1.0f - static_cast<float>(y - margin) / trackHeight;
        // クリック時はスムーズに移動（タイマーを起動）
        m_value = qBound(0.0f, val, 1.0f);
        if (!m_animTimer.isActive()) {
            m_animTimer.start();
        }
        emit valueChanged(m_value);
        update();
    }
}

void FaderWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging) {
        int margin = 8;
        int trackHeight = height() - margin * 2;
        int y = qBound(margin, event->pos().y(), height() - margin);
        
        float val = 1.0f - static_cast<float>(y - margin) / trackHeight;
        m_displayValue = qBound(0.0f, val, 1.0f); // ドラッグ中は即座に追従
        m_value = m_displayValue;
        emit valueChanged(m_value);
        update();
    }
}

void FaderWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
        update();
    }
}

void FaderWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    // ダブルクリックで 0dB（デフォルト）へスムーズにリセット
    m_value = FaderWidget::dbToPosition(0.0f);
    if (!m_animTimer.isActive()) {
        m_animTimer.start();
    }
    emit valueChanged(m_value);
}
