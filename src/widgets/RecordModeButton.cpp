#include "RecordModeButton.h"

#include "common/ThemeManager.h"
#include <QFile>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QSvgRenderer>

namespace {
constexpr int kGestureStartDistancePx = 6;

QPixmap renderSvg(const QString& resourcePath, const QColor& color, const QSize& size, qreal dpr)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QString svg = QString::fromUtf8(file.readAll());
    const QString colorName = color.name();
    svg.replace(QLatin1String("stroke=\"currentColor\""),
                QStringLiteral("stroke=\"%1\"").arg(colorName));
    svg.replace(QLatin1String("fill=\"currentColor\""),
                QStringLiteral("fill=\"%1\"").arg(colorName));

    QSvgRenderer renderer(svg.toUtf8());
    QPixmap pixmap(size * dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    // Draw using high quality antialiasing
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter);
    painter.end();
    
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

QString iconPathForMode(RecordModeButton::Mode mode)
{
    return mode == RecordModeButton::Mode::Midi
        ? QStringLiteral(":/icons/piano.svg")
        : QStringLiteral(":/icons/mic.svg");
}
}

RecordModeButton::RecordModeButton(QWidget* parent)
    : QWidget(parent)
    , m_slotAnimation(new QPropertyAnimation(this, "slotProgress", this))
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setFixedSize(40, 40);

    m_slotAnimation->setDuration(180);
    m_slotAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(&Darwin::ThemeManager::instance(), &Darwin::ThemeManager::themeChanged,
            this, [this]() { update(); });
}

void RecordModeButton::setMode(Mode mode)
{
    if (m_mode == mode) {
        return;
    }

    m_mode = mode;
    m_previewMode = mode;
    update();
}

void RecordModeButton::setRecording(bool recording)
{
    if (m_recording == recording) {
        return;
    }

    m_recording = recording;
    update();
}

void RecordModeButton::setSlotProgress(qreal progress)
{
    const qreal clamped = qBound<qreal>(0.0, progress, 1.0);
    if (qFuzzyCompare(m_slotProgress, clamped)) {
        return;
    }

    m_slotProgress = clamped;
    update();
}

void RecordModeButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const Darwin::ThemeManager& tm = Darwin::ThemeManager::instance();
    const bool isDark = tm.isDarkMode();
    const QColor accent = QColor(QStringLiteral("#FF3366"));
    const QColor border = m_recording ? accent : tm.borderColor();
    const QColor bg = m_recording
        ? QColor(accent.red(), accent.green(), accent.blue(), isDark ? 70 : 48)
        : QColor(tm.panelBackgroundColor().red(),
                 tm.panelBackgroundColor().green(),
                 tm.panelBackgroundColor().blue(),
                 isDark ? 220 : 245);
    const QColor slotBg = m_recording
        ? QColor(255, 255, 255, 28)
        : QColor(tm.backgroundColor().red(),
                 tm.backgroundColor().green(),
                 tm.backgroundColor().blue(),
                 isDark ? 160 : 235);
    const QColor iconColor = m_recording ? QColor(Qt::white) : tm.textColor();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF outer = rect().adjusted(1.0, 1.0, -1.0, -1.0);
    p.setPen(QPen(border, 1.2));
    p.setBrush(bg);
    p.drawRoundedRect(outer, 16.0, 16.0);

    const QRectF inner = outer.adjusted(6.0, 6.0, -6.0, -6.0);
    if (m_switchGesture || m_slotProgress > 0.001) {
        p.setPen(Qt::NoPen);
        p.setBrush(slotBg);
        p.drawRoundedRect(inner, 10.0, 10.0);
    }

    // ジェスチャ中は「押下開始時のモード」をベースにスロット演出を描く。
    // 実際の m_mode はドラッグ中に即時更新されるので、描画基準を分けておかないと
    // 切り替え途中でアイコンの入れ替わりが不自然になる。
    const Mode slotBaseMode = m_switchGesture ? m_gestureOriginMode : m_mode;
    const Mode slotPreviewMode = alternateModeFor(slotBaseMode);
    const qreal slotDirection = m_slotDirection;

    const QSize iconSize(18, 18);
    const qreal dpr = devicePixelRatioF();
    const QPixmap currentPixmap = renderSvg(iconPathForMode(slotBaseMode), iconColor, iconSize, dpr);
    const QPixmap previewPixmap = renderSvg(iconPathForMode(slotPreviewMode), iconColor, iconSize, dpr);

    auto drawIcon = [&p, &iconSize](const QPixmap& pixmap, const QPointF& center, qreal opacity) {
        if (pixmap.isNull() || opacity <= 0.0) {
            return;
        }
        p.save();
        p.setOpacity(opacity);
        const QRectF target(center.x() - iconSize.width() * 0.5,
                            center.y() - iconSize.height() * 0.5,
                            iconSize.width(),
                            iconSize.height());
        // Draw with smooth transform using SourceRect = pixmap.rect()
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawPixmap(target, pixmap, QRectF(pixmap.rect()));
        p.restore();
    };

    const QPointF baseCenter = outer.center();
    const qreal progress = m_switchGesture ? m_slotProgress : 0.0;

    drawIcon(currentPixmap,
             QPointF(baseCenter.x(), baseCenter.y() - slotDirection * progress * 7.0),
             1.0 - progress * 0.25);

    drawIcon(previewPixmap,
             QPointF(baseCenter.x(),
                     baseCenter.y() + slotDirection * (16.0 - progress * 16.0)),
             progress);
}

void RecordModeButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_pressed = true;
    m_switchGesture = false;
    m_previewMode = m_mode;
    m_gestureOriginMode = m_mode;
    m_pressPos = event->pos();
    m_slotDirection = -1.0;
    event->accept();
}

void RecordModeButton::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_pressed) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (!m_recording && !m_switchGesture) {
        const int deltaY = event->pos().y() - m_pressPos.y();
        // 長押し待ちではなく、上下どちらでも一定距離ドラッグした時点で
        // モード切替ジェスチャへ入る。
        const bool shouldStartGesture = qAbs(deltaY) >= kGestureStartDistancePx;
        if (shouldStartGesture) {
            m_switchGesture = true;
        }
    }

    if (m_switchGesture) {
        updatePreviewMode(event->pos());
    }
    event->accept();
}

void RecordModeButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_pressed || event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    const bool inside = rect().contains(event->pos());
    if (!m_switchGesture && inside) {
        emit clicked();
    }

    m_pressed = false;
    m_switchGesture = false;
    m_previewMode = m_mode;
    animateSlotProgress(0.0);
    event->accept();
}

void RecordModeButton::animateSlotProgress(qreal target)
{
    if (!m_slotAnimation) {
        setSlotProgress(target);
        return;
    }

    m_slotAnimation->stop();
    m_slotAnimation->setStartValue(m_slotProgress);
    m_slotAnimation->setEndValue(target);
    m_slotAnimation->start();
}

void RecordModeButton::updatePreviewMode(const QPoint& pos)
{
    const int deltaY = pos.y() - m_pressPos.y();
    if (deltaY < 0) {
        m_slotDirection = -1.0;
    } else if (deltaY > 0) {
        m_slotDirection = 1.0;
    }

    const bool shouldSwitch = qAbs(deltaY) >= kGestureStartDistancePx;
    const Mode nextMode = shouldSwitch
        ? alternateModeFor(m_gestureOriginMode)
        : m_gestureOriginMode;
    if (m_previewMode != nextMode) {
        m_previewMode = nextMode;
    }
    if (m_mode != nextMode) {
        // ボタンを離す前に実モードも更新しておくことで、
        // ツールチップや録音モード判定が押下中の見た目と一致する。
        m_mode = nextMode;
        emit modeChanged(m_mode);
    }
    animateSlotProgress(m_previewMode == m_gestureOriginMode ? 0.0 : 1.0);
}

RecordModeButton::Mode RecordModeButton::alternateModeFor(Mode mode)
{
    return mode == Mode::Audio ? Mode::Midi : Mode::Audio;
}
