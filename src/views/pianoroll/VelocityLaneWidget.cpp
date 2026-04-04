#include "VelocityLaneWidget.h"
#include "Clip.h"
#include "Note.h"
#include <QPainter>
#include <QMouseEvent>
#include "common/Constants.h"
#include "common/ThemeManager.h"

using namespace Darwin;

// Height reserved for velocity lane
static const int VELOCITY_LANE_HEIGHT = 60;

VelocityLaneWidget::VelocityLaneWidget(QWidget *parent)
    : QWidget(parent)
    , m_activeClip(nullptr)
    , m_interactingNote(nullptr)
    , m_isDragging(false)
{
    setMinimumWidth(1600); // 16 bars
    setMinimumHeight(VELOCITY_LANE_HEIGHT);
    setMouseTracking(true);

    // スムーズアニメーション用タイマー（60fps）
    connect(&m_animTimer, &QTimer::timeout, this, &VelocityLaneWidget::tickAnimation);
    m_animTimer.setInterval(UI_ANIMATION_INTERVAL_MS);
}

QSize VelocityLaneWidget::sizeHint() const
{
    return QSize(1600, VELOCITY_LANE_HEIGHT);
}

void VelocityLaneWidget::setActiveClip(Clip* clip)
{
    if (m_activeClip) {
        disconnect(m_activeClip, nullptr, this, nullptr);
    }
    
    m_activeClip = clip;
    m_interactingNote = nullptr;
    m_displayVelocities.clear();
    
    if (m_activeClip) {
        connect(m_activeClip, &Clip::changed, this, [this](){ update(); });
        connect(m_activeClip, &QObject::destroyed, this, [this]() {
            m_activeClip = nullptr;
            m_interactingNote = nullptr;
            m_isDragging = false;
            m_displayVelocities.clear();
            update();
        });
    }
    
    update();
}

void VelocityLaneWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    
    // Background
    p.fillRect(rect(), Darwin::ThemeManager::instance().backgroundColor());
    
    // Top border
    p.setPen(Darwin::ThemeManager::instance().borderColor());
    p.drawLine(0, 0, width(), 0);

    // ───── 目盛り（メモリ）─────
    {
        const int h = height() - 4;
        QColor scaleLineColor = Darwin::ThemeManager::instance().secondaryTextColor();
        scaleLineColor.setAlpha(40);

        // 高さに応じてステップを選択（最小18px間隔を確保）
        static const int steps[] = {8, 16, 32, 64};
        int step = 64;
        for (int s : steps) {
            double pxPerStep = static_cast<double>(h) * s / 127.0;
            if (pxPerStep >= 18.0) {
                step = s;
                break;
            }
        }

        for (int val = 0; val <= 127; val += step) {
            double ratio = static_cast<double>(val) / 127.0;
            int y = 2 + static_cast<int>((1.0 - ratio) * h);
            bool isMajor = (val == 0 || val == 64 || val == 127);
            p.setPen(QPen(scaleLineColor, 1, isMajor ? Qt::DashLine : Qt::DotLine));
            p.drawLine(0, y, width(), y);
        }
        // 127はステップの倍数でない場合があるので常に描画
        if (127 % step != 0) {
            int y = 2;
            p.setPen(QPen(scaleLineColor, 1, Qt::DashLine));
            p.drawLine(0, y, width(), y);
        }
    }

    if (!m_activeClip) return;
    
    // Draw velocity bars with smooth animation
    bool anyAnimating = false;
    for (Note* note : m_activeClip->notes()) {
        const QColor accentColor = Darwin::ThemeManager::instance().accentColor();
        int x = static_cast<int>(note->startTick() * PIXELS_PER_TICK);
        
        // 目標比率
        double targetRatio = static_cast<double>(note->velocity()) / 127.0;
        
        // 表示用比率を取得（初回は目標に直接設定）
        float displayRatio;
        if (m_displayVelocities.contains(note)) {
            displayRatio = m_displayVelocities[note];
        } else {
            displayRatio = static_cast<float>(targetRatio);
            m_displayVelocities[note] = displayRatio;
        }
        
        // 差分があればアニメーション中
        if (qAbs(displayRatio - static_cast<float>(targetRatio)) > 0.002f) {
            anyAnimating = true;
        }
        
        int barHeight = static_cast<int>(displayRatio * (height() - 4));
        int y = height() - barHeight;
        
        int w = 4; // Width of velocity bar line
        
        // ステムをグラデーシン新付きで描画
        QColor stemColor = (note == m_interactingNote)
            ? accentColor.lighter(180)
            : Darwin::ThemeManager::instance().secondaryTextColor();
        p.setPen(stemColor);
        p.drawLine(x + 2, y, x + 2, height());
        
        // Draw top rect (handle)
        QColor noteColor = (note == m_interactingNote) ? accentColor.lighter(125) : accentColor;
        p.setPen(Qt::NoPen);
        p.setBrush(noteColor);
        p.drawRoundedRect(x, y, w, 5, 1, 1);
    }
    
    // アニメーション中はタイマーを起動
    if (anyAnimating && !m_animTimer.isActive()) {
        m_animTimer.start();
    }

    // クリップ範囲外をグレーアウト
    {
        const int clipEndX = static_cast<int>(m_activeClip->durationTicks() * PIXELS_PER_TICK);
        QColor outOfRange(0, 0, 0, Darwin::ThemeManager::instance().isDarkMode() ? 80 : 30);
        if (clipEndX < width()) {
            p.fillRect(clipEndX, 0, width() - clipEndX, height(), outOfRange);
        }
    }
}

void VelocityLaneWidget::mousePressEvent(QMouseEvent *event)
{
    if (!m_activeClip) return;
    
    m_interactingNote = nullptr;
    
    // Find note near horizontally
    const auto& notes = m_activeClip->notes();
    for (int i = notes.size() - 1; i >= 0; --i) {
        Note* note = notes[i];
        int x = static_cast<int>(note->startTick() * PIXELS_PER_TICK);
        
        // Tolerance for clicking near the bar
        if (qAbs(event->pos().x() - x) < 6) {
            m_interactingNote = note;
            m_isDragging = true;
            updateVelocityFromMouse(event->pos());
            break;
        }
    }
    
    update();
}

void VelocityLaneWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging && m_interactingNote) {
        updateVelocityFromMouse(event->pos());
    }
}

void VelocityLaneWidget::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    m_isDragging = false;
    m_interactingNote = nullptr;
    update();
}

void VelocityLaneWidget::updateVelocityFromMouse(const QPoint& pos)
{
    if (!m_interactingNote) return;
    
    int y = pos.y();
    y = qBound(2, y, height() - 2);
    
    // Invert mapping: higher Y means lower velocity
    double ratio = 1.0 - (static_cast<double>(y - 2) / (height() - 4));
    int velocity = static_cast<int>(ratio * 127.0);
    velocity = qBound(0, velocity, 127);
    
    m_interactingNote->setVelocity(velocity);
    // アニメーションタイマーを起動してスムーズに表示を追従させる
    if (!m_animTimer.isActive()) {
        m_animTimer.start();
    }
}

void VelocityLaneWidget::tickAnimation()
{
    if (!m_activeClip) {
        m_animTimer.stop();
        return;
    }
    
    bool anyAnimating = false;
    for (Note* note : m_activeClip->notes()) {
        float targetRatio = static_cast<float>(note->velocity()) / 127.0f;
        float& displayRatio = m_displayVelocities[note];
        
        float diff = targetRatio - displayRatio;
        if (qAbs(diff) < 0.002f) {
            displayRatio = targetRatio;
        } else {
            displayRatio += diff * 0.3f; // スムーズ補間
            anyAnimating = true;
        }
    }
    
    if (!anyAnimating) {
        m_animTimer.stop();
    }
    
    update();
}
