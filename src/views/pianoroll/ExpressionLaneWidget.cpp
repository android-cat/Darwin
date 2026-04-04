#include "ExpressionLaneWidget.h"
#include "Clip.h"
#include "CCEvent.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include "common/Constants.h"
#include "common/ThemeManager.h"

using namespace Darwin;

static const int LANE_HEIGHT = 60;
static const int POINT_RADIUS = 4;

ExpressionLaneWidget::ExpressionLaneWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumWidth(1600);
    setFixedHeight(LANE_HEIGHT);
    setMouseTracking(true);
}

QSize ExpressionLaneWidget::sizeHint() const
{
    return QSize(1600, LANE_HEIGHT);
}

void ExpressionLaneWidget::setCCNumber(int ccNumber)
{
    if (m_ccNumber != ccNumber) {
        m_ccNumber = ccNumber;
        m_dragEvent = nullptr;
        m_isDragging = false;
        update();
    }
}

void ExpressionLaneWidget::setActiveClip(Clip* clip)
{
    if (m_activeClip) {
        disconnect(m_activeClip, nullptr, this, nullptr);
    }

    m_activeClip = clip;
    m_dragEvent = nullptr;
    m_isDragging = false;

    if (m_activeClip) {
        connect(m_activeClip, &Clip::changed, this, [this](){ update(); });
        connect(m_activeClip, &QObject::destroyed, this, [this]() {
            m_activeClip = nullptr;
            m_dragEvent = nullptr;
            m_isDragging = false;
            update();
        });
    }

    update();
}

void ExpressionLaneWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& tm = ThemeManager::instance();

    // 背景
    p.fillRect(rect(), tm.backgroundColor());

    // 上端ボーダー
    p.setPen(tm.borderColor());
    p.drawLine(0, 0, width(), 0);

    if (!m_activeClip) return;

    const QList<CCEvent*> events = m_activeClip->ccEventsForCC(m_ccNumber);

    // Pitch Bend の場合はセンターライン表示
    const int cv = centerValue();
    if (cv >= 0) {
        int cy = yFromValue(cv);
        QColor centerColor = tm.secondaryTextColor();
        centerColor.setAlpha(80);
        p.setPen(QPen(centerColor, 1, Qt::DashLine));
        p.drawLine(0, cy, width(), cy);
    }

    if (events.isEmpty()) return;

    // ブレークポイント同士を線で結ぶ
    const QColor lineColor = tm.accentColor();
    QColor fillColor = lineColor;
    fillColor.setAlpha(30);

    // ティック順でソート済みのコピーを作る
    QList<CCEvent*> sorted = events;
    std::sort(sorted.begin(), sorted.end(),
              [](const CCEvent* a, const CCEvent* b) { return a->tick() < b->tick(); });

    // カーブパスの構築
    QPainterPath curvePath;
    QPainterPath fillPath;
    const int baseline = (cv >= 0) ? yFromValue(cv) : height() - 2;

    for (int i = 0; i < sorted.size(); ++i) {
        int x = xFromTick(sorted[i]->tick());
        int y = yFromValue(sorted[i]->value());

        if (i == 0) {
            curvePath.moveTo(x, y);
            fillPath.moveTo(x, baseline);
            fillPath.lineTo(x, y);
        } else {
            curvePath.lineTo(x, y);
            fillPath.lineTo(x, y);
        }
    }

    // 塗り潰し（最後のポイントからベースラインに降ろして閉じる）
    if (!sorted.isEmpty()) {
        int lastX = xFromTick(sorted.last()->tick());
        fillPath.lineTo(lastX, baseline);
        fillPath.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(fillColor);
        p.drawPath(fillPath);
    }

    // 線
    p.setPen(QPen(lineColor, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawPath(curvePath);

    // ポイントの描画
    for (const CCEvent* ev : sorted) {
        int x = xFromTick(ev->tick());
        int y = yFromValue(ev->value());

        QColor ptColor = (ev == m_dragEvent) ? lineColor.lighter(140) : lineColor;
        p.setPen(QPen(ptColor, 1.5));
        p.setBrush(ptColor);
        p.drawEllipse(QPoint(x, y), POINT_RADIUS, POINT_RADIUS);
    }
}

void ExpressionLaneWidget::mousePressEvent(QMouseEvent *event)
{
    if (!m_activeClip || event->button() != Qt::LeftButton) return;

    CCEvent* near = findNearEvent(event->pos());
    if (near) {
        m_dragEvent = near;
        m_isDragging = true;
    } else {
        // 新しいポイントを追加
        qint64 tick = tickFromX(event->pos().x());
        int value = valueFromY(event->pos().y());
        CCEvent* newEvent = m_activeClip->addCCEvent(m_ccNumber, tick, value);
        m_dragEvent = newEvent;
        m_isDragging = true;
    }
    update();
}

void ExpressionLaneWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging && m_dragEvent) {
        qint64 tick = tickFromX(event->pos().x());
        int value = valueFromY(event->pos().y());
        m_dragEvent->setTick(tick);
        m_dragEvent->setValue(value);
        update();
    }
}

void ExpressionLaneWidget::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    m_isDragging = false;
    m_dragEvent = nullptr;
    update();
}

void ExpressionLaneWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_activeClip || event->button() != Qt::LeftButton) return;

    // ダブルクリックでポイントを削除
    CCEvent* near = findNearEvent(event->pos());
    if (near) {
        m_activeClip->removeCCEvent(near);
        m_dragEvent = nullptr;
        m_isDragging = false;
        update();
    }
}

// ───── ヘルパー ─────

int ExpressionLaneWidget::valueFromY(int y) const
{
    int h = height() - 4;
    double ratio = 1.0 - (static_cast<double>(y - 2) / h);
    ratio = qBound(0.0, ratio, 1.0);
    return static_cast<int>(ratio * maxValue());
}

int ExpressionLaneWidget::yFromValue(int value) const
{
    int h = height() - 4;
    double ratio = static_cast<double>(value) / maxValue();
    return 2 + static_cast<int>((1.0 - ratio) * h);
}

qint64 ExpressionLaneWidget::tickFromX(int x) const
{
    return qMax(0LL, static_cast<qint64>(x / PIXELS_PER_TICK));
}

int ExpressionLaneWidget::xFromTick(qint64 tick) const
{
    return static_cast<int>(tick * PIXELS_PER_TICK);
}

int ExpressionLaneWidget::maxValue() const
{
    if (m_ccNumber == CCEvent::CC_PITCH_BEND) {
        return 16383;
    }
    return 127;
}

int ExpressionLaneWidget::centerValue() const
{
    if (m_ccNumber == CCEvent::CC_PITCH_BEND) {
        return 8192;
    }
    return -1; // センターラインなし
}

CCEvent* ExpressionLaneWidget::findNearEvent(const QPoint& pos, int tolerance) const
{
    if (!m_activeClip) return nullptr;

    const QList<CCEvent*> events = m_activeClip->ccEventsForCC(m_ccNumber);
    CCEvent* best = nullptr;
    int bestDist = tolerance + 1;

    for (CCEvent* ev : events) {
        int ex = xFromTick(ev->tick());
        int ey = yFromValue(ev->value());
        int dist = qAbs(pos.x() - ex) + qAbs(pos.y() - ey); // マンハッタン距離
        if (dist < bestDist) {
            bestDist = dist;
            best = ev;
        }
    }
    return best;
}
