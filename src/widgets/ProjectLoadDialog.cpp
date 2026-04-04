#include "ProjectLoadDialog.h"
#include <QPainter>
#include <QPainterPath>
#include <QApplication>
#include <cmath>
#include "common/Constants.h"
#include "common/FontManager.h"
#include "common/ThemeManager.h"

static float easeOutCubic(float t) { return 1.0f - std::pow(1.0f - t, 3.0f); }
static float easeOutBack(float t) { const float c = 1.70158f; return 1.0f + (c+1)*std::pow(t-1,3) + c*std::pow(t-1,2); }

// ── DAW カラーパレット (ThemeManager 経由) ──
// 個別の色定数は廃止し、paintEvent 内で ThemeManager から直接取得します。

ProjectLoadDialog::ProjectLoadDialog(const QString& projectName, QWidget* parent)
    : QDialog(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_projectName(projectName)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(true);
    setFixedSize(360, 180);

    m_clock.start();
    m_stageStartMs = m_clock.elapsed();

    connect(&m_timer, &QTimer::timeout, this, &ProjectLoadDialog::tick);
    m_timer.setInterval(Darwin::UI_ANIMATION_INTERVAL_MS);
    m_timer.start();
}

void ProjectLoadDialog::showSuccess(int trackCount)
{
    m_trackCount = trackCount;
    m_stage = Success;
    m_stageStartMs = m_clock.elapsed();
}

void ProjectLoadDialog::showFailure(const QString& reason)
{
    m_failReason = reason;
    m_stage = Failure;
    m_stageStartMs = m_clock.elapsed();
}

void ProjectLoadDialog::tick()
{
    qint64 now = m_clock.elapsed();
    float stageElapsed = static_cast<float>(now - m_stageStartMs);

    if (m_stage == Loading) {
        float time = static_cast<float>(now) * 0.001f;
        m_spinnerAngle = std::fmod(time * 360.0f, 360.0f);
        m_progressSweep = std::fmod(time * 0.8f, 1.0f);
    }

    if (m_stage == Success && stageElapsed > 1200.0f) {
        m_stage = FadeOut;
        m_stageStartMs = now;
    }
    if (m_stage == Failure && stageElapsed > 2000.0f) {
        m_stage = FadeOut;
        m_stageStartMs = now;
    }
    if (m_stage == FadeOut) {
        float t = qBound(0.0f, stageElapsed / 350.0f, 1.0f);
        m_overlayAlpha = 1.0f - easeOutCubic(t);
        if (t >= 1.0f) {
            m_timer.stop();
            accept();
            return;
        }
    }

    update();
}

void ProjectLoadDialog::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    float ga = m_overlayAlpha;
    qint64 now = m_clock.elapsed();
    float se = static_cast<float>(now - m_stageStartMs);

    QRectF card(16, 16, width() - 32, height() - 32);

    // ── ドロップシャドウ ──
    p.save();
    p.setOpacity(ga);
    for (int i = 3; i >= 1; --i) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 6 * i));
        p.drawRoundedRect(card.adjusted(-i*2, -i*1.5, i*2, i*2.5), 12 + i, 12 + i);
    }
    p.restore();

    // ── カード ──
    p.save();
    p.setOpacity(ga);

    auto& tm = Darwin::ThemeManager::instance();
    QColor cardBg = tm.panelBackgroundColor();
    QColor borderColor = tm.borderColor();
    QColor accentColor = tm.accentColor();
    QColor textPrimary = tm.textColor();
    QColor textSecondary = tm.secondaryTextColor();
    QColor successCol = tm.successColor();
    QColor errorCol = tm.errorColor();

    p.setPen(QPen(borderColor, 1));
    p.setBrush(cardBg);
    p.drawRoundedRect(card, 12, 12);

    // 上部アクセントライン (#FF3366)
    QPainterPath accentBar;
    accentBar.addRoundedRect(card.left() + 20, card.top() + 1.5, card.width() - 40, 2.5, 1.2, 1.2);
    p.setPen(Qt::NoPen);
    p.setBrush(accentColor);
    p.drawPath(accentBar);
    p.restore();

    QFont titleFont = Darwin::FontManager::uiFont(11, QFont::Bold);
    QFont subFont = Darwin::FontManager::uiFont(9);
    QFont smallFont = Darwin::FontManager::uiFont(8);

    // ── Loading ──
    if (m_stage == Loading) {
        p.save();
        p.setOpacity(ga);

        // プロジェクト名
        p.setFont(titleFont);
        p.setPen(textPrimary);
        p.drawText(card.adjusted(20, 20, -20, 0), Qt::AlignTop | Qt::AlignHCenter, m_projectName);

        // サブテキスト
        p.setFont(subFont);
        p.setPen(textSecondary);
        p.drawText(card.adjusted(20, 44, -20, 0), Qt::AlignTop | Qt::AlignHCenter,
                   "Loading project...");

        // ── 回転スピナーリング (アクセントカラー) ──
        QPointF center(card.center().x(), card.center().y() + 14);
        float outerR = 16.0f;
        float penW = 2.5f;

        // 背景リング (ダーク)
        p.setPen(QPen(QColor(51, 65, 85), penW, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(center, outerR, outerR);

        // アクセントアーク (回転)
        float sweepDeg = 90.0f + 60.0f * std::sin(m_progressSweep * 6.2832f);
        QRectF arcRect(center.x() - outerR, center.y() - outerR, outerR * 2, outerR * 2);
        p.setPen(QPen(accentColor, penW, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(arcRect, static_cast<int>(m_spinnerAngle * 16), static_cast<int>(sweepDeg * 16));

        p.restore();
    }

    // ── Success ──
    else if (m_stage == Success) {
        p.save();
        p.setOpacity(ga);
        float t = qBound(0.0f, se / 450.0f, 1.0f);
        float ease = easeOutBack(t);

        QPointF center(card.center().x(), card.center().y() - 2);

        // サークル (#FF3366 → 成功色にモーフ)
        float circleR = 18.0f * ease;
        QColor circleCol = successCol;
        circleCol.setAlphaF(ease * 0.9);
        p.setPen(Qt::NoPen);
        p.setBrush(circleCol);
        p.drawEllipse(center, circleR, circleR);

        // チェックマーク (白)
        if (t > 0.25f) {
            float ct = qBound(0.0f, (t - 0.25f) / 0.75f, 1.0f);
            p.setPen(QPen(Qt::white, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

            QPointF p1(center.x() - 8, center.y() + 1);
            QPointF p2(center.x() - 2, center.y() + 7);
            QPointF p3(center.x() + 8, center.y() - 5);

            float s1 = qBound(0.0f, ct * 2.2f, 1.0f);
            float s2 = qBound(0.0f, ct * 2.2f - 1.0f, 1.0f);

            if (s1 > 0) {
                QPointF m(p1.x() + (p2.x()-p1.x())*s1, p1.y() + (p2.y()-p1.y())*s1);
                p.drawLine(p1, m);
            }
            if (s2 > 0) {
                QPointF m(p2.x() + (p3.x()-p2.x())*s2, p2.y() + (p3.y()-p2.y())*s2);
                p.drawLine(p2, m);
            }
        }

        // テキスト
        float ta = qBound(0.0f, (se - 250.0f) / 350.0f, 1.0f);
        p.setFont(subFont);
        QColor tc = textPrimary;
        tc.setAlphaF(ta);
        p.setPen(tc);
        p.drawText(card.adjusted(20, 0, -20, -14), Qt::AlignBottom | Qt::AlignHCenter,
                   QString("%1 tracks loaded").arg(m_trackCount));

        // リングパルス
        if (t < 1.0f) {
            float rp = 18.0f + t * 35.0f;
            QColor ring = successCol;
            ring.setAlphaF((1.0f - t) * 0.35f);
            p.setPen(QPen(ring, 1.5 * (1.0f - t)));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(center, rp, rp);
        }

        p.restore();
    }

    // ── Failure ──
    else if (m_stage == Failure) {
        p.save();
        p.setOpacity(ga);
        float t = qBound(0.0f, se / 400.0f, 1.0f);
        float ease = easeOutCubic(t);

        QPointF center(card.center().x(), card.center().y() - 2);
        float circleR = 18.0f * ease;
        QColor cc = errorCol;
        cc.setAlphaF(ease * 0.9);
        p.setPen(Qt::NoPen);
        p.setBrush(cc);
        p.drawEllipse(center, circleR, circleR);

        if (t > 0.3f) {
            float ct = qBound(0.0f, (t - 0.3f) / 0.7f, 1.0f);
            p.setPen(QPen(Qt::white, 2.5, Qt::SolidLine, Qt::RoundCap));
            float s = 7.0f;
            p.drawLine(QPointF(center.x()-s, center.y()-s), QPointF(center.x()+s, center.y()+s));
            p.drawLine(QPointF(center.x()+s, center.y()-s), QPointF(center.x()-s, center.y()+s));
        }

        float ta = qBound(0.0f, (se - 250.0f) / 350.0f, 1.0f);
        p.setFont(subFont);
        QColor ftc = errorCol;
        ftc.setAlphaF(ta * 0.85f);
        p.setPen(ftc);
        p.drawText(card.adjusted(20, 0, -20, -14), Qt::AlignBottom | Qt::AlignHCenter,
                   m_failReason.isEmpty() ? "Failed to load project" : m_failReason);

        p.restore();
    }
}
