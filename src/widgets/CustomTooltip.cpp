#include "CustomTooltip.h"
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QScreen>
#include <QEvent>
#include <QHelpEvent>
#include <QLineEdit>
#include <QLabel>
#include <QFontMetrics>
#include "common/FontManager.h"

CustomTooltip* CustomTooltip::instance() {
    static CustomTooltip* s_inst = nullptr;
    if (!s_inst) {
        s_inst = new CustomTooltip();
    }
    return s_inst;
}

void CustomTooltip::attach(QWidget* w) {
    if (!w) return;
    // 重複登録を避けるため一度削除してから登録
    w->removeEventFilter(instance());
    w->installEventFilter(instance());
}

CustomTooltip::CustomTooltip() 
    : QLabel(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet(QString(R"(
        QLabel {
            background-color: #1e293b;
            color: #e2e8f0;
            border: 1px solid #334155;
            border-radius: 4px;
            padding: 5px 10px;
            font-family: %1;
            font-size: 11px;
            font-weight: 600;
        }
    )").arg(Darwin::FontManager::uiFontCss()));

    auto* eff = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(eff);

    m_fadeAnim = new QPropertyAnimation(eff, "opacity", this);
    m_fadeAnim->setDuration(200);
    m_fadeAnim->setEasingCurve(QEasingCurve::OutQuad);
    
    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(4000); 
    connect(m_hideTimer, &QTimer::timeout, this, &CustomTooltip::hideText);
}

void CustomTooltip::showText(const QPoint& globalPos, const QString& text) {
    if (text.isEmpty()) {
        hideText();
        return;
    }
    
    auto* inst = instance();
    inst->m_fadeAnim->disconnect(); // 古いコールバックの解除
    inst->m_hideTimer->stop();
    
    inst->setText(text);
    inst->adjustSize();

    // マウスカーソルの直下に中心が来るように表示
    QPoint pos = globalPos + QPoint(-(inst->width() / 2), 16);
    if (QScreen *screen = QGuiApplication::screenAt(globalPos)) {
        QRect screenRect = screen->availableGeometry();
        if (pos.x() < screenRect.left()) {
            pos.setX(screenRect.left() + 5);
        } else if (pos.x() + inst->width() > screenRect.right()) {
            pos.setX(screenRect.right() - inst->width() - 5);
        }
        
        if (pos.y() + inst->height() > screenRect.bottom()) {
            pos.setY(globalPos.y() - inst->height() - 5);
        }
    }

    inst->move(pos);
    inst->show();

    inst->m_fadeAnim->stop();
    inst->m_fadeAnim->setStartValue(0.0);
    inst->m_fadeAnim->setEndValue(1.0);
    inst->m_fadeAnim->start();
    
    inst->m_hideTimer->start();
}

void CustomTooltip::hideText() {
    auto* inst = instance();
    if (!inst->isVisible()) return;

    inst->m_hideTimer->stop();
    inst->m_fadeAnim->stop();
    inst->m_fadeAnim->disconnect();

    inst->m_fadeAnim->setStartValue(inst->graphicsEffect()->property("opacity").toDouble());
    inst->m_fadeAnim->setEndValue(0.0);
    
    connect(inst->m_fadeAnim, &QPropertyAnimation::finished, inst, &QWidget::hide);
    inst->m_fadeAnim->start();
}

bool CustomTooltip::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        auto* helpEvent = static_cast<QHelpEvent*>(event);
        QString text;
        
        // 元のウィジェットの幅をオーバーしている場合にのみツールチップを表示する
        if (auto* le = qobject_cast<QLineEdit*>(watched)) {
            text = le->text();
            int textW = le->fontMetrics().horizontalAdvance(text);
            if (textW <= le->width() - 4) {
                return true; 
            }
        } 
        else if (auto* lbl = qobject_cast<QLabel*>(watched)) {
            text = lbl->text();
            int textW = lbl->fontMetrics().horizontalAdvance(text);
            if (textW <= lbl->width() - 4) { 
                return true; 
            }
        }
        
        if (!text.isEmpty()) {
            showText(helpEvent->globalPos(), text);
        }
        return true; 
    } 
    else if (event->type() == QEvent::Leave) {
        hideText();
    }
    else if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::KeyPress) {
        hideText();
    }
    return false;
}
