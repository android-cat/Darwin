#include "ExportProgressDialog.h"
#include "../common/ThemeManager.h"
#include "../common/FontManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPropertyAnimation>
#include <QApplication>
#include <QMouseEvent>

ExportProgressDialog::ExportProgressDialog(const QString& infoText, QWidget* parent)
    : QDialog(parent),
      m_progress(0.0),
      m_animatedProgress(0.0),
      m_infoText(infoText)
{
    setFixedSize(600, 420); // サイズを画像に合わせて少し大きく
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);

    m_anim = new QPropertyAnimation(this, "animatedProgress", this);
    m_anim->setEasingCurve(QEasingCurve::OutQuad);
    m_anim->setDuration(300);
}

ExportProgressDialog::~ExportProgressDialog()
{
}

void ExportProgressDialog::setProgress(double progress)
{
    m_progress = progress;
    m_anim->stop();
    m_anim->setStartValue(m_animatedProgress);
    m_anim->setEndValue(progress);
    m_anim->start();
    update();
}

void ExportProgressDialog::setInfoText(const QString& text)
{
    m_infoText = text;
    update();
}

void ExportProgressDialog::setAnimatedProgress(double p)
{
    m_animatedProgress = p;
    update();
}

void ExportProgressDialog::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void ExportProgressDialog::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
}

void ExportProgressDialog::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    auto& tm = Darwin::ThemeManager::instance();
    bool isDark = tm.isDarkMode();

    // 全体背景（画像は白）
    p.fillRect(rect(), isDark ? tm.backgroundColor() : QColor("#ffffff"));

    // ─── ヘッダー描画 ───
    const int headerHeight = 40;
    QRect headerRect(0, 0, width(), headerHeight);
    p.fillRect(headerRect, QColor("#221f1f")); // 画像の暗いヘッダー色

    p.setPen(Qt::white);
    p.setFont(Darwin::FontManager::uiFont(11, QFont::Medium));
    p.drawText(headerRect.adjusted(15, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, "エクスポート中...");

    // ─── メインテキスト ───
    p.setPen(isDark ? tm.textColor() : QColor("#334155"));
    p.setFont(Darwin::FontManager::uiFont(12));
    QRect textRect(20, headerHeight + 20, width() - 40, 30);
    p.drawText(textRect, Qt::AlignCenter, m_infoText);

    // 紙領域
    const double paperWidth = 300;
    const double paperHeight = 220;
    const double marginX = (width() - paperWidth) / 2.0;
    const double marginY = headerHeight + 70;
    QRectF paperRect(marginX, marginY, paperWidth, paperHeight);

    drawPaperAndLines(p, paperRect);

    // 進行状況パーセント（画像は下の方に薄いブルーグレー）
    p.setPen(isDark ? tm.secondaryTextColor() : QColor("#94a3b8"));
    p.setFont(Darwin::FontManager::uiFont(14, QFont::Light));
    QRect percentRect(20, height() - 50, width() - 40, 40);
    p.drawText(percentRect, Qt::AlignCenter, QString("%1 %").arg(static_cast<int>(m_progress * 100)));

    // 外枠（少し丸みのあるボーダー）
    p.setPen(QPen(QColor(0, 0, 0, 50), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect().adjusted(0,0,-1,-1));
}

void ExportProgressDialog::drawPaperAndLines(QPainter& p, const QRectF& paperRect) const
{
    auto& tm = Darwin::ThemeManager::instance();
    bool isDark = tm.isDarkMode();

    // 紙の見た目（白、丸角、薄い影）
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 30));
    p.drawRoundedRect(paperRect.translated(0, 4), 12, 12); // 影

    p.setBrush(isDark ? QColor("#1e293b") : Qt::white);
    p.setPen(QPen(isDark ? QColor("#334155") : QColor("#e2e8f0"), 1.5));
    p.drawRoundedRect(paperRect, 12, 12);
    p.restore();

    // 行の設定
    const int numLines = 8;
    const double linePadding = 30;
    const double lineSpacing = (paperRect.height() - linePadding * 2) / (numLines - 1);
    const double lineStartX = paperRect.left() + 30;
    const double lineEndX = paperRect.right() - 30;
    const double lineLen = lineEndX - lineStartX;

    const double totalLineLength = numLines * lineLen;
    const double currentDrawnLength = totalLineLength * m_animatedProgress;

    int currentLineIdx = static_cast<int>(currentDrawnLength / lineLen);
    double currentLineFraction = (currentDrawnLength - (currentLineIdx * lineLen)) / lineLen;

    if (currentLineIdx >= numLines) {
        currentLineIdx = numLines - 1;
        currentLineFraction = 1.0;
    }

    QPointF penTipPos;

    for (int i = 0; i < numLines; ++i) {
        double y = paperRect.top() + linePadding + i * lineSpacing;

        // ガイドライン
        p.setPen(QPen(isDark ? QColor("#475569") : QColor("#cbd5e1"), 1.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(lineStartX, y), QPointF(lineEndX, y));

        // 書き込み済みの太い線
        if (i < currentLineIdx) {
            p.setPen(QPen(isDark ? tm.textColor() : QColor("#1e293b"), 3, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(lineStartX, y), QPointF(lineEndX, y));
        } else if (i == currentLineIdx) {
            double drawnX = lineStartX + lineLen * currentLineFraction;
            p.setPen(QPen(isDark ? tm.textColor() : QColor("#1e293b"), 3, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(lineStartX, y), QPointF(drawnX, y));
            penTipPos = QPointF(drawnX, y);
        }
    }

    if (m_animatedProgress < 1.0) {
        if (penTipPos.isNull()) {
            penTipPos = QPointF(lineStartX, paperRect.top() + linePadding);
        }
        drawPen(p, penTipPos);
    }
}

void ExportProgressDialog::drawPen(QPainter& p, const QPointF& tipPos) const
{
    p.save();
    p.translate(tipPos);
    p.rotate(25);

    auto& tm = Darwin::ThemeManager::instance();
    bool isDark = tm.isDarkMode();

    // 画像カラー
    QColor penBodyColor = Darwin::ThemeManager::instance().accentColor();
    QColor penWhiteColor = QColor("#ffffff");

    QColor actualBody = penBodyColor;
    QColor actualTip = penWhiteColor;
    QColor actualCap = penBodyColor; // お尻もピンク赤に固定

    // ペン先（白い三角形）
    QPainterPath tipPath;
    tipPath.moveTo(0, 0);
    tipPath.lineTo(8, -18);
    tipPath.lineTo(-8, -18);
    tipPath.closeSubpath();

    p.setBrush(actualTip);
    p.setPen(QPen(QColor("#cbd5e1"), 0.5));
    p.drawPath(tipPath);

    // 芯（ペン先の極小の点）
    p.setBrush(Qt::black);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(0, 0), 1.5, 1.5);

    // ペン本体（ピンク長方形）
    QRectF bodyRect(-8, -75, 16, 57);
    p.setBrush(actualBody);
    p.setPen(Qt::NoPen);
    p.drawRect(bodyRect);

    // お尻のキャップ（白 or 黒）
    QRectF capRect(-8, -80, 16, 5);
    p.setBrush(actualCap);
    p.setPen(isDark ? Qt::NoPen : QPen(QColor("#cbd5e1"), 0.5));
    p.drawRect(capRect);

    p.restore();
}
