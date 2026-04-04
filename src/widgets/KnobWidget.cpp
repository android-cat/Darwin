#include "KnobWidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <QtMath>
#include "common/Constants.h"
#include "common/FontManager.h"
#include "common/ThemeManager.h"

KnobWidget::KnobWidget(const QString &label, QWidget *parent)
    : QWidget(parent)
    , m_label(label)
    , m_value(0.5f)
    , m_displayValue(0.5f)
    , m_isDragging(false)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // テーマ変更時に再描画
    connect(&Darwin::ThemeManager::instance(), &Darwin::ThemeManager::themeChanged,
            this, [this]() { update(); });

    // スムーズアニメーション用タイマー
    connect(&m_animTimer, &QTimer::timeout, this, &KnobWidget::tickAnimation);
    m_animTimer.setInterval(Darwin::UI_ANIMATION_INTERVAL_MS);
}

void KnobWidget::setValue(float value)
{
    float bounded = qBound(0.0f, value, 1.0f);
    if (!qFuzzyCompare(m_value, bounded)) {
        m_value = bounded;
        if (m_isDragging) {
            m_displayValue = m_value;
        } else if (!m_animTimer.isActive()) {
            m_animTimer.start();
        }
        update();
        emit valueChanged(m_value);
    }
}

void KnobWidget::tickAnimation()
{
    float diff = m_value - m_displayValue;
    if (qAbs(diff) < 0.002f) {
        m_displayValue = m_value;
        m_animTimer.stop();
    } else {
        m_displayValue += diff * 0.25f;
    }
    update();
}

QSize KnobWidget::sizeHint() const
{
    return QSize(40, 50); // w-10 approx
}

void KnobWidget::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    int w = width();
    int h = height();
    int knobSize = 24;
    int x = (w - knobSize) / 2;
    int y = 14; 

    // Label (Top)
    const Darwin::ThemeManager& tm = Darwin::ThemeManager::instance();
    p.setPen(tm.secondaryTextColor());
    p.setFont(Darwin::FontManager::uiFont(7, QFont::Bold));
    QRect labelRect(0, 0, w, 12);
    p.drawText(labelRect, Qt::AlignHCenter | Qt::AlignVCenter, m_label);

    // Knob Circle background arc (inactive portion)
    QRect knobRect(x, y, knobSize, knobSize);
    
    p.setPen(QPen(tm.borderColor(), 2.5, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    int arcStart = static_cast<int>(-225 * 16);
    int arcSpan = static_cast<int>(270 * 16);
    p.drawArc(knobRect.adjusted(2, 2, -2, -2), arcStart, arcSpan);

    // アクティブなアーク（値の部分）
    if (m_displayValue > 0.01f) {
        int activeSpan = static_cast<int>(m_displayValue * 270 * 16);
        QColor activeColor = tm.accentColor();
        if (m_isDragging) {
            activeColor = activeColor.lighter(130);
        }
        p.setPen(QPen(activeColor, 2.5, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(knobRect.adjusted(2, 2, -2, -2), arcStart, activeSpan);
    }

    // Knob Circle
    p.setPen(QPen(tm.borderColor(), 1));
    p.setBrush(tm.panelBackgroundColor());
    p.drawEllipse(knobRect);

    // Indicator
    float angle = -135.0f + (m_displayValue * 270.0f);
    float radians = qDegreesToRadians(angle - 90.0f);
    
    // Center point
    QPointF center = knobRect.center();
    float radius = (knobSize / 2.0f) - 4.0f; // inner padding
    
    float indX = center.x() + radius * qCos(radians);
    float indY = center.y() + radius * qSin(radians);
    
    p.setPen(QPen(tm.textColor(), 2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(center, QPointF(indX, indY));
}

void KnobWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        m_lastMousePos = event->pos();
        update();
    }
}

void KnobWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging) {
        int dy = m_lastMousePos.y() - event->pos().y();
        m_lastMousePos = event->pos();
        
        float sensitivity = 0.005f;
        float newVal = qBound(0.0f, m_value + (dy * sensitivity), 1.0f);
        m_value = newVal;
        m_displayValue = newVal; // ドラッグ中は即座に追従
        emit valueChanged(m_value);
        update();
    }
}

void KnobWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
        update();
    }
}

void KnobWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    // ダブルクリックでセンター(0.5)にスムーズリセット
    m_value = 0.5f;
    if (!m_animTimer.isActive()) {
        m_animTimer.start();
    }
    emit valueChanged(m_value);
}
