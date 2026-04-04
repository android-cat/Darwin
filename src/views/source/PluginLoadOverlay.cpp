/**
 * @file PluginLoadOverlay.cpp
 * @brief VST3プラグインロード中のオーバーレイウィジェット実装
 */
#include "SourceView.h"
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include "common/Constants.h"
#include "common/FontManager.h"
#include "common/ThemeManager.h"

// ─── PluginLoadOverlay ───────────────────────────────────

static float eoC(float t) { return 1.0f - std::pow(1.0f - t, 3.0f); }
static float eoB(float t) { const float c = 1.70158f; return 1.0f + (c+1)*std::pow(t-1,3) + c*std::pow(t-1,2); }

static const QColor kOvBg(255, 255, 255);
static const QColor kOvBorder(226, 232, 240);
static const QColor kOvText(51, 51, 51);
static const QColor kOvSub(148, 163, 184);
static const QColor kOvAccent(255, 51, 102);
static const QColor kOvSuccess(34, 197, 94);
static const QColor kOvFailure(239, 68, 68);

PluginLoadOverlay::PluginLoadOverlay(QWidget* parent)
    : QWidget(parent)
{
    hide();
    connect(&m_timer, &QTimer::timeout, this, &PluginLoadOverlay::tick);
}

void PluginLoadOverlay::startLoading(const QString& pluginName)
{
    m_pluginName = pluginName;
    m_stage = Loading;
    m_overlayAlpha = 0.0f;
    m_spinnerAngle = 0.0f;
    m_progressSweep = 0.0f;
    m_wavePhase = 0.0f;
    m_failReason.clear();

    if (parentWidget())
        setGeometry(parentWidget()->rect());
    raise();
    show();

    m_clock.start();
    m_stageStartMs = m_clock.elapsed();
    m_timer.start(Darwin::UI_ANIMATION_INTERVAL_MS);
}

void PluginLoadOverlay::showSuccess()
{
    m_stage = Success;
    m_stageStartMs = m_clock.elapsed();
}

void PluginLoadOverlay::showFailure(const QString& reason)
{
    m_failReason = reason;
    m_stage = Failure;
    m_stageStartMs = m_clock.elapsed();
}

void PluginLoadOverlay::tick()
{
    qint64 now = m_clock.elapsed();
    float se = static_cast<float>(now - m_stageStartMs);

    if (m_overlayAlpha < 1.0f)
        m_overlayAlpha = qBound(0.0f, static_cast<float>(now) / 220.0f, 1.0f);

    if (m_stage == Loading) {
        float time = static_cast<float>(now) * 0.001f;
        m_spinnerAngle = std::fmod(time * 360.0f, 360.0f);
        m_progressSweep = std::fmod(time * 0.8f, 1.0f);
        m_wavePhase = time * 4.0f;
    }
    if (m_stage == Success && se > 1200.0f) {
        m_stage = FadeOut;
        m_stageStartMs = now;
    }
    if (m_stage == Failure && se > 2200.0f) {
        m_stage = FadeOut;
        m_stageStartMs = now;
    }
    if (m_stage == FadeOut) {
        float t = qBound(0.0f, se / 350.0f, 1.0f);
        m_overlayAlpha = 1.0f - eoC(t);
        if (t >= 1.0f) {
            m_timer.stop();
            hide();
            emit finished();
            return;
        }
    }
    update();
}

void PluginLoadOverlay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    qint64 now = m_clock.elapsed();
    float se  = static_cast<float>(now - m_stageStartMs);
    float ga  = m_overlayAlpha;

    QColor ovBg = Darwin::ThemeManager::instance().backgroundColor();
    QColor ovBorder = Darwin::ThemeManager::instance().borderColor();
    QColor ovText = Darwin::ThemeManager::instance().textColor();
    QColor ovSub = Darwin::ThemeManager::instance().secondaryTextColor();
    QColor ovAccent = Darwin::ThemeManager::instance().accentColor();
    QColor ovSuccess = QColor(34, 197, 94);
    QColor ovFailure = QColor(239, 68, 68);

    // ── 背景 ──
    p.save();
    p.setOpacity(ga * 0.96);
    p.setPen(Qt::NoPen);
    p.setBrush(ovBg);
    p.drawRect(rect());
    p.restore();

    QRectF area(rect());
    QPointF center(area.center().x(), area.center().y() - 16);
    QFont titleFont = Darwin::FontManager::uiFont(14, QFont::DemiBold);
    QFont subFont = Darwin::FontManager::uiFont(10);

    // ── Loading ──
    if (m_stage == Loading) {
        p.save();
        p.setOpacity(ga);

        // プラグイン名
        p.setFont(titleFont);
        p.setPen(ovText);
        QRectF nameRect(0, center.y() - 80, width(), 28);
        p.drawText(nameRect, Qt::AlignCenter, m_pluginName);

        // ── 回転スピナーリング ──
        float outerR = 22.0f;
        float penW   = 3.0f;

        // 背景リング
        p.setPen(QPen(ovBorder, penW, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(center, outerR, outerR);

        // アクセントアーク
        float sweepDeg = 90.0f + 60.0f * std::sin(m_progressSweep * 6.2832f);
        QRectF arcRect(center.x() - outerR, center.y() - outerR,
                       outerR * 2, outerR * 2);
        p.setPen(QPen(ovAccent, penW, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(arcRect,
                  static_cast<int>(m_spinnerAngle * 16),
                  static_cast<int>(sweepDeg * 16));

        // サブテキスト
        p.setFont(subFont);
        p.setPen(ovSub);
        p.drawText(QRectF(0, center.y() + 34, width(), 22),
                   Qt::AlignCenter, "Loading instrument\u2026");

        // ── 波形バー (音楽的装飾) ──
        const int barCount = 5;
        const float barW = 4.0f;
        const float barSpacing = 7.0f;
        float totalW = barCount * barW + (barCount - 1) * barSpacing;
        float startX = center.x() - totalW / 2.0f;
        float barBaseY = center.y() + 68.0f;

        for (int i = 0; i < barCount; ++i) {
            float phase = m_wavePhase + i * 0.9f;
            float h = 5.0f + 14.0f * std::abs(std::sin(phase));
            QRectF bar(startX + i * (barW + barSpacing),
                       barBaseY - h / 2.0f, barW, h);

            QColor bc = ovAccent;
            bc.setAlphaF(0.35f + 0.35f * std::abs(std::sin(phase)));
            p.setPen(Qt::NoPen);
            p.setBrush(bc);
            p.drawRoundedRect(bar, 2, 2);
        }

        p.restore();
    }

    // ── Success ──
    else if (m_stage == Success) {
        p.save();
        p.setOpacity(ga);
        float t    = qBound(0.0f, se / 400.0f, 1.0f);
        float ease = eoB(t);

        // 緑サークル
        float circleR = 22.0f * ease;
        QColor cc = ovSuccess;
        cc.setAlphaF(ease * 0.9f);
        p.setPen(Qt::NoPen);
        p.setBrush(cc);
        p.drawEllipse(center, circleR, circleR);

        // チェックマーク
        if (t > 0.2f) {
            float ct = qBound(0.0f, (t - 0.2f) / 0.8f, 1.0f);
            p.setPen(QPen(Qt::white, 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

            QPointF p1(center.x() - 9, center.y() + 1);
            QPointF p2(center.x() - 3, center.y() + 8);
            QPointF p3(center.x() + 9, center.y() - 6);

            float s1 = qBound(0.0f, ct * 2.2f, 1.0f);
            float s2 = qBound(0.0f, ct * 2.2f - 1.0f, 1.0f);
            if (s1 > 0) {
                QPointF m(p1.x()+(p2.x()-p1.x())*s1, p1.y()+(p2.y()-p1.y())*s1);
                p.drawLine(p1, m);
            }
            if (s2 > 0) {
                QPointF m(p2.x()+(p3.x()-p2.x())*s2, p2.y()+(p3.y()-p2.y())*s2);
                p.drawLine(p2, m);
            }
        }

        // リングパルス
        if (t < 1.0f) {
            float rp = 22.0f + t * 40.0f;
            QColor ring = ovSuccess;
            ring.setAlphaF((1.0f - t) * 0.3f);
            p.setPen(QPen(ring, 1.5f * (1.0f - t)));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(center, rp, rp);
        }

        // テキスト
        float ta = qBound(0.0f, (se - 200.0f) / 300.0f, 1.0f);
        p.setFont(titleFont);
        QColor tc = ovText;
        tc.setAlphaF(ta);
        p.setPen(tc);
        p.drawText(QRectF(0, center.y() + 38, width(), 28),
                   Qt::AlignCenter, "Loaded");

        p.setFont(subFont);
        QColor sc = ovSub;
        sc.setAlphaF(ta);
        p.setPen(sc);
        p.drawText(QRectF(0, center.y() + 60, width(), 20),
                   Qt::AlignCenter, m_pluginName);

        p.restore();
    }

    // ── Failure ──
    else if (m_stage == Failure) {
        p.save();
        p.setOpacity(ga);
        float t    = qBound(0.0f, se / 400.0f, 1.0f);
        float ease = eoC(t);

        // 赤サークル
        float circleR = 22.0f * ease;
        QColor cc = ovFailure;
        cc.setAlphaF(ease * 0.9f);
        p.setPen(Qt::NoPen);
        p.setBrush(cc);
        p.drawEllipse(center, circleR, circleR);

        // ×マーク
        if (t > 0.25f) {
            p.setPen(QPen(Qt::white, 2.8, Qt::SolidLine, Qt::RoundCap));
            float s = 8.0f;
            p.drawLine(QPointF(center.x()-s, center.y()-s),
                       QPointF(center.x()+s, center.y()+s));
            p.drawLine(QPointF(center.x()+s, center.y()-s),
                       QPointF(center.x()-s, center.y()+s));
        }

        // テキスト
        float ta = qBound(0.0f, (se - 200.0f) / 350.0f, 1.0f);
        p.setFont(subFont);
        QColor ftc = ovFailure;
        ftc.setAlphaF(ta * 0.9f);
        p.setPen(ftc);
        QString msg = m_failReason.isEmpty() ? "Failed to load plugin" : m_failReason;
        p.drawText(QRectF(20, center.y() + 38, width() - 40, 40),
                   Qt::AlignCenter | Qt::TextWordWrap, msg);

        p.restore();
    }
}
