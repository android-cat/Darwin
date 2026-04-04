#include "PluginEditorWidget.h"
#include "VST3PluginInstance.h"
#ifdef Q_OS_MAC
#include "MacNativeViewUtils.h"
#endif

// VST3 SDK ヘッダー
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/base/funknownimpl.h"

#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QWindow>
#include <QScreen>
#include <QApplication>
#include <QDebug>
#include <QPainter>
#include <QPaintEvent>
#include <QEvent>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QResizeEvent>

#ifdef Q_OS_WIN
#include <Windows.h>
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
#endif

using namespace Steinberg;

// ウィジェットのDPIスケール倍率を取得するユーティリティ
// Qt6のHigh DPIスケーリングにより、Qt論理ピクセルとデバイスピクセルにずれが生じる。
// ただし macOS の NSView 埋め込みは Cocoa の point 座標で扱うため 1.0 固定にする。
static double getDevicePixelRatio(const QWidget* widget)
{
#ifdef Q_OS_MAC
    Q_UNUSED(widget);
    return 1.0;
#else
    if (widget && widget->screen()) {
        return widget->screen()->devicePixelRatio();
    }
    return qApp->devicePixelRatio();
#endif
}

#ifdef Q_OS_MAC
// macOS の native viewport / container に clip を再同期する。
// QScrollArea のスクロール後も NSView が sibling 領域へはみ出さないようにする。
static void syncMacNativeClipState(QScrollArea* scrollArea, QWidget* container)
{
    if (container) {
        Darwin::prepareMacClipWidget(container);
        Darwin::synchronizeMacClipWidget(container);
    }

    Q_UNUSED(scrollArea);
}
#endif

// ========== IPlugFrame実装 ==========

// IPlugFrame実装：プラグインからのリサイズ要求を処理
class DarwinPlugFrame : public U::ImplementsNonDestroyable<U::Directly<IPlugFrame>>
{
public:
    DarwinPlugFrame(PluginEditorWidget* widget) : m_widget(widget) {}

    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override
    {
        if (!view || !newSize) return kInvalidArgument;

        int w = newSize->right - newSize->left;
        int h = newSize->bottom - newSize->top;

        if (m_widget) {
            m_widget->updateContainerSize(w, h);
        }

        view->onSize(newSize);
        return kResultTrue;
    }

private:
    PluginEditorWidget* m_widget;
};

static DarwinPlugFrame* s_currentPlugFrame = nullptr;

// ========== ScaledPluginDisplay ==========

ScaledPluginDisplay::ScaledPluginDisplay(QWidget* parent)
    : QWidget(parent)
    , m_pluginHwnd(nullptr)
    , m_scaleFactor(1.0)
    , m_nativeWidth(0)
    , m_nativeHeight(0)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_NoMousePropagation);
    setFocusPolicy(Qt::ClickFocus);
    setStyleSheet("background-color: #1a1a2e;");
}

void ScaledPluginDisplay::setPixmap(const QPixmap& pixmap)
{
    m_pixmap = pixmap;
    update();
}

void ScaledPluginDisplay::setPluginHwnd(void* hwnd) { m_pluginHwnd = hwnd; }
void ScaledPluginDisplay::setScaleFactor(double factor) { m_scaleFactor = factor; }
void ScaledPluginDisplay::setNativeSize(int w, int h) { m_nativeWidth = w; m_nativeHeight = h; }

void ScaledPluginDisplay::paintEvent(QPaintEvent*)
{
    if (m_pixmap.isNull()) return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawPixmap(rect(), m_pixmap);
}

QPoint ScaledPluginDisplay::scaleToNative(const QPoint& pos) const
{
    if (m_scaleFactor <= 0.0) return pos;
    // 表示座標（Qt論理ピクセル）をプラグインネイティブ座標（デバイスピクセル）に変換
    // 表示サイズ = logicalNative * scaleFactor なので
    // ネイティブ座標 = 表示座標 / scaleFactor * dpr
    double dpr = getDevicePixelRatio(this);
    return QPoint(
        static_cast<int>(pos.x() / m_scaleFactor * dpr),
        static_cast<int>(pos.y() / m_scaleFactor * dpr)
    );
}

void ScaledPluginDisplay::forwardMouseEvent(QMouseEvent* event, unsigned int msg)
{
#ifdef Q_OS_WIN
    if (!m_pluginHwnd) return;
    HWND containerHwnd = reinterpret_cast<HWND>(m_pluginHwnd);
    QPoint native = scaleToNative(event->pos());

    // プラグインの子ウィンドウを検索（実際のUI要素がある場所）
    POINT pt = {native.x(), native.y()};
    HWND targetHwnd = ChildWindowFromPointEx(
        containerHwnd, pt, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
    if (!targetHwnd) targetHwnd = containerHwnd;

    // ターゲットウィンドウのクライアント座標に変換
    if (targetHwnd != containerHwnd) {
        MapWindowPoints(containerHwnd, targetHwnd, &pt, 1);
    }

    LPARAM lParam = MAKELPARAM(pt.x, pt.y);
    WPARAM wParam = 0;
    if (event->buttons() & Qt::LeftButton)      wParam |= MK_LBUTTON;
    if (event->buttons() & Qt::RightButton)     wParam |= MK_RBUTTON;
    if (event->buttons() & Qt::MiddleButton)    wParam |= MK_MBUTTON;
    if (event->modifiers() & Qt::ControlModifier) wParam |= MK_CONTROL;
    if (event->modifiers() & Qt::ShiftModifier)   wParam |= MK_SHIFT;

    PostMessage(targetHwnd, msg, wParam, lParam);
#else
    Q_UNUSED(event);
    Q_UNUSED(msg);
#endif
}

void ScaledPluginDisplay::mousePressEvent(QMouseEvent* event)
{
#ifdef Q_OS_WIN
    unsigned int msg = WM_LBUTTONDOWN;
    if (event->button() == Qt::RightButton)       msg = WM_RBUTTONDOWN;
    else if (event->button() == Qt::MiddleButton) msg = WM_MBUTTONDOWN;
    forwardMouseEvent(event, msg);
#endif
    event->accept();
    emit userInteracted();
}

void ScaledPluginDisplay::mouseReleaseEvent(QMouseEvent* event)
{
#ifdef Q_OS_WIN
    unsigned int msg = WM_LBUTTONUP;
    if (event->button() == Qt::RightButton)       msg = WM_RBUTTONUP;
    else if (event->button() == Qt::MiddleButton) msg = WM_MBUTTONUP;
    forwardMouseEvent(event, msg);
#endif
    event->accept();
    emit userInteracted();
}

void ScaledPluginDisplay::mouseMoveEvent(QMouseEvent* event)
{
#ifdef Q_OS_WIN
    forwardMouseEvent(event, WM_MOUSEMOVE);
#else
    Q_UNUSED(event);
#endif
    event->accept();
}

void ScaledPluginDisplay::mouseDoubleClickEvent(QMouseEvent* event)
{
#ifdef Q_OS_WIN
    unsigned int msg = WM_LBUTTONDBLCLK;
    if (event->button() == Qt::RightButton)       msg = WM_RBUTTONDBLCLK;
    else if (event->button() == Qt::MiddleButton) msg = WM_MBUTTONDBLCLK;
    forwardMouseEvent(event, msg);
#endif
    event->accept();
    emit userInteracted();
}

void ScaledPluginDisplay::wheelEvent(QWheelEvent* event)
{
#ifdef Q_OS_WIN
    if (!m_pluginHwnd) return;
    HWND containerHwnd = reinterpret_cast<HWND>(m_pluginHwnd);
    QPoint pos = event->position().toPoint();
    QPoint native = scaleToNative(pos);
    LPARAM lParam = MAKELPARAM(native.x(), native.y());
    WPARAM wParam = MAKEWPARAM(0, static_cast<short>(event->angleDelta().y()));
    PostMessage(containerHwnd, WM_MOUSEWHEEL, wParam, lParam);
#else
    Q_UNUSED(event);
#endif
    event->accept();
    emit userInteracted();
}

// ========== PluginEditorWidget ==========

PluginEditorWidget::PluginEditorWidget(QWidget* parent)
    : QWidget(parent)
    , m_plugView(nullptr)
    , m_plugFrame(nullptr)
    , m_container(nullptr)
    , m_scrollArea(nullptr)
    , m_scaleLabel(nullptr)
    , m_offscreenHost(nullptr)
    , m_scaledDisplay(nullptr)
    , m_scaleFactor(1.0)
    , m_nativeWidth(0)
    , m_nativeHeight(0)
    , m_supportsResize(false)
    , m_bitmapCaptureActive(false)
    , m_deferredViewSyncPending(false)
{
    // ウィジェットが親レイアウト内で最大限に広がるように設定
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);

    // スケール率表示ラベル（右下にオーバーレイ表示）
    m_scaleLabel = new QLabel(this);
    m_scaleLabel->setStyleSheet(
        "QLabel { background-color: rgba(0,0,0,160); color: #fff; "
        "padding: 2px 8px; border-radius: 4px; font-size: 11px; }");
    m_scaleLabel->setAlignment(Qt::AlignCenter);
    m_scaleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_scaleLabel->hide();

    // キャプチャタイマー接続
    connect(&m_captureTimer, &QTimer::timeout,
            this, &PluginEditorWidget::capturePluginView);
}

PluginEditorWidget::~PluginEditorWidget()
{
    closeEditor();
}

void PluginEditorWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // スクロールエリアをウィジェット全体に広げる
    if (m_scrollArea) {
        m_scrollArea->setGeometry(rect());
    }

    // スケールラベルの位置を右下に配置（スクロールエリアより手前に表示）
    if (m_scaleLabel) {
        m_scaleLabel->adjustSize();
        m_scaleLabel->move(width() - m_scaleLabel->width() - 8,
                           height() - m_scaleLabel->height() - 8);
        m_scaleLabel->raise();
    }

    // プラグインが開いていればスケールモードを更新
    if (m_plugView && m_nativeWidth > 0 && m_nativeHeight > 0) {
        updateScaleMode();
        scheduleDeferredViewSync();
    }

#ifdef Q_OS_MAC
    syncMacNativeClipState(m_scrollArea, m_container);
#endif
}

bool PluginEditorWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (shouldScheduleViewSync(watched, event)) {
        scheduleDeferredViewSync();
#ifdef Q_OS_MAC
        syncMacNativeClipState(m_scrollArea, m_container);
#endif
    }

    if (m_plugView && shouldConsumePriorityEvent(watched, event)) {
        event->accept();
        return true;
    }

    return QWidget::eventFilter(watched, event);
}

bool PluginEditorWidget::shouldScheduleViewSync(QObject* watched, QEvent* event) const
{
    if (!m_plugView || !watched || !event) {
        return false;
    }

    const QWidget* viewport = m_scrollArea ? m_scrollArea->viewport() : nullptr;
    if (watched != m_scrollArea && watched != viewport && watched != m_container) {
        return false;
    }

    switch (event->type()) {
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::ShowToParent:
        return true;
    default:
        return false;
    }
}

void PluginEditorWidget::updateScaleMode()
{
    if (!m_plugView || m_nativeWidth <= 0 || m_nativeHeight <= 0) return;

    int availW = m_scrollArea ? m_scrollArea->viewport()->width() : width();
    int availH = m_scrollArea ? m_scrollArea->viewport()->height() : height();
    if (availW <= 0 || availH <= 0) return;

    // プラグインのネイティブサイズ（デバイスピクセル）をQt論理ピクセルに変換
    double dpr = getDevicePixelRatio(this);
    double logicalNativeW = m_nativeWidth / dpr;
    double logicalNativeH = m_nativeHeight / dpr;

    // アスペクト比を保持しつつウィジェットに最大限フィットするスケール倍率を計算
    // 注意: スケール1.0 = プラグインのネイティブサイズ（DPI補正後）
    double scaleW = static_cast<double>(availW) / logicalNativeW;
    double scaleH = static_cast<double>(availH) / logicalNativeH;
    double newScale = qMin(scaleW, scaleH);

    // スケールラベル更新
    int pct = static_cast<int>(newScale * 100.0 + 0.5);
    if (pct >= 98 && pct <= 102) {
        m_scaleLabel->hide();
    } else {
        m_scaleLabel->setText(QString("%1%").arg(pct));
        m_scaleLabel->adjustSize();
        m_scaleLabel->move(width() - m_scaleLabel->width() - 8,
                           height() - m_scaleLabel->height() - 8);
        m_scaleLabel->raise();
        m_scaleLabel->show();
    }

    m_scaleFactor = newScale;

    if (m_supportsResize) {
        // canResize対応: プラグインをウィジェットサイズに合わせてリサイズ
        // プラグインにはデバイスピクセル単位で希望サイズを伝える
        int desiredW = qMax(1, static_cast<int>(m_nativeWidth * newScale));
        int desiredH = qMax(1, static_cast<int>(m_nativeHeight * newScale));

        ViewRect rect {};
        rect.right = desiredW;
        rect.bottom = desiredH;
        m_plugView->checkSizeConstraint(&rect);
        m_plugView->onSize(&rect);

        // プラグインが返したサイズ（デバイスピクセル）をQt論理ピクセルに変換
        int actualDevW = rect.right - rect.left;
        int actualDevH = rect.bottom - rect.top;
        int logicalW = qMax(1, static_cast<int>(actualDevW / dpr));
        int logicalH = qMax(1, static_cast<int>(actualDevH / dpr));
        if (m_container) {
            m_container->setFixedSize(logicalW, logicalH);
            m_container->updateGeometry();
        }
        // スクロールバーを必要に応じて表示（パネルが小さい場合に対応）
        if (m_scrollArea) {
            m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }
        if (m_bitmapCaptureActive) {
            enterDirectMode();
        }
    } else {
        // canResize非対応: ネイティブサイズ固定でスクロールエリア表示
        // プラグインHWNDを直接操作するため、PostMessageによる迂回は行わず
        // ネイティブサイズのまま表示してスクロールで閲覧できるようにする
        int logicalW = qMax(1, static_cast<int>(logicalNativeW));
        int logicalH = qMax(1, static_cast<int>(logicalNativeH));

        if (m_bitmapCaptureActive) {
            enterDirectMode();
        }

        // スケール倍率は常に1.0（ネイティブサイズ固定）
        m_scaleFactor = 1.0;
        m_scaleLabel->hide();

        // コンテナをネイティブサイズに設定（スクロールエリアがはみ出しを処理）
        if (m_container) {
            m_container->setFixedSize(logicalW, logicalH);
            m_container->updateGeometry();
        }
        // スクロールバーを必要に応じて表示
        if (m_scrollArea) {
            m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }
    }

#ifdef Q_OS_MAC
    syncMacNativeClipState(m_scrollArea, m_container);
#endif
}

void PluginEditorWidget::scheduleDeferredViewSync()
{
    if (!m_plugView || m_deferredViewSyncPending) {
        return;
    }

    m_deferredViewSyncPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_deferredViewSyncPending = false;
        if (!m_plugView || m_nativeWidth <= 0 || m_nativeHeight <= 0) {
            return;
        }

        updateScaleMode();
    });
}

void PluginEditorWidget::setPluginInputPriority(bool active)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, !active);

    if (m_scrollArea) {
        m_scrollArea->removeEventFilter(this);
        m_scrollArea->setAttribute(Qt::WA_NoMousePropagation, false);

        QWidget* viewport = m_scrollArea->viewport();
        if (viewport) {
            viewport->removeEventFilter(this);
            viewport->setAttribute(Qt::WA_NoMousePropagation, false);
            if (active) {
                m_scrollArea->installEventFilter(this);
                viewport->installEventFilter(this);
            }
        }
    }

    if (m_container) {
        m_container->setAttribute(Qt::WA_NoMousePropagation, active);
        m_container->removeEventFilter(this);
        if (active) {
            m_container->installEventFilter(this);
        }
    }

    if (m_scaledDisplay) {
        m_scaledDisplay->setAttribute(Qt::WA_NoMousePropagation, active);
    }
}

bool PluginEditorWidget::shouldConsumePriorityEvent(QObject* watched, QEvent* event) const
{
    if (!watched || !event) {
        return false;
    }

    const QWidget* viewport = m_scrollArea ? m_scrollArea->viewport() : nullptr;
    if (watched != m_container && watched != viewport) {
        return false;
    }

    auto eventPosInWatched = [event]() -> QPoint {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
            return static_cast<QMouseEvent*>(event)->position().toPoint();
        case QEvent::Wheel:
            return static_cast<QWheelEvent*>(event)->position().toPoint();
        case QEvent::ContextMenu:
            return static_cast<QContextMenuEvent*>(event)->pos();
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease:
            return static_cast<QTabletEvent*>(event)->position().toPoint();
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd: {
            const auto& points = static_cast<QTouchEvent*>(event)->points();
            if (!points.isEmpty()) {
                return points.constFirst().position().toPoint();
            }
            break;
        }
        default:
            break;
        }

        return QPoint(-1, -1);
    };

    // viewport 全体を丸ごと優先すると splitter や余白まで巻き込むので、
    // 実際のプラグイン表示領域に入っているときだけ親UIへの伝播を止める。
    if (watched == viewport && m_container) {
        const QPoint eventPos = eventPosInWatched();
        if (eventPos.x() < 0 || !m_container->geometry().contains(eventPos)) {
            return false;
        }
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::ContextMenu:
    case QEvent::NativeGesture:
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
        return true;
    default:
        return false;
    }
}

void PluginEditorWidget::updateContainerSize(int w, int h)
{
    // プラグインからのリサイズ通知 — ネイティブサイズ（デバイスピクセル）を更新
    m_nativeWidth = w;
    m_nativeHeight = h;

    // デバイスピクセル → Qt論理ピクセル変換
    double dpr = getDevicePixelRatio(this);
    int logicalW = qMax(1, static_cast<int>(w / dpr));
    int logicalH = qMax(1, static_cast<int>(h / dpr));

    if (m_container) {
        m_container->setFixedSize(logicalW, logicalH);
        m_container->updateGeometry();
    }
    if (m_offscreenHost) {
        // オフスクリーンホストはプラグインの実描画用なのでネイティブサイズ
        m_offscreenHost->setFixedSize(logicalW, logicalH);
    }

    // ビットマップキャプチャモードならスケーリング表示も更新
    if (m_bitmapCaptureActive && m_scaledDisplay) {
        m_scaledDisplay->setNativeSize(w, h);
        updateScaleMode();
    }
    scheduleDeferredViewSync();

#ifdef Q_OS_MAC
    syncMacNativeClipState(m_scrollArea, m_container);
#endif
}

bool PluginEditorWidget::openEditor(VST3PluginInstance* instance)
{
    if (!instance || !instance->isLoaded()) {
        qWarning() << "PluginEditor: Plugin not loaded or instance null";
        return false;
    }

    qDebug() << "PluginEditor: Attempting to open editor for" << instance->pluginName();
    closeEditor();

    // IPlugViewを取得
    m_plugView = instance->createView();
    if (!m_plugView) {
        qWarning() << "PluginEditor: createView() returned NULL";
        return false;
    }

    qDebug() << "PluginEditor: IPlugView created successfully";

#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
    const char* platformType = nullptr;
#ifdef Q_OS_WIN
    platformType = kPlatformTypeHWND;
#elif defined(Q_OS_MAC)
    platformType = kPlatformTypeNSView;
#endif

    if (!platformType ||
        m_plugView->isPlatformTypeSupported(platformType) != kResultTrue) {
        qWarning() << "PluginEditor: 対応するプラットフォームビューがありません";
        m_plugView->release();
        m_plugView = nullptr;
        return false;
    }

    // canResize確認
    m_supportsResize = (m_plugView->canResize() == kResultTrue);
    qDebug() << "PluginEditor: canResize =" << m_supportsResize;

    // IPlugFrameを設定
    if (s_currentPlugFrame) delete s_currentPlugFrame;
    s_currentPlugFrame = new DarwinPlugFrame(this);
    m_plugFrame = s_currentPlugFrame;
    m_plugView->setFrame(m_plugFrame);

    // ネイティブサイズを取得
    ViewRect viewRect {};
    m_nativeWidth = 800;
    m_nativeHeight = 600;
    if (m_plugView->getSize(&viewRect) == kResultTrue) {
        int w = viewRect.right - viewRect.left;
        int h = viewRect.bottom - viewRect.top;
        if (w > 0 && h > 0) {
            m_nativeWidth = w;
            m_nativeHeight = h;
        }
    }

    // DPI倍率を取得し、デバイスピクセル→Qt論理ピクセルに変換
    double dpr = getDevicePixelRatio(this);
    int logicalW = qMax(1, static_cast<int>(m_nativeWidth / dpr));
    int logicalH = qMax(1, static_cast<int>(m_nativeHeight / dpr));
    qDebug() << "PluginEditor: Native size:" << m_nativeWidth << "x" << m_nativeHeight
             << "DPR:" << dpr << "Logical:" << logicalW << "x" << logicalH;

    // スクロールエリアを作成（コンテナを内包してスクロール可能にする）
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(false);  // コンテナサイズは手動管理
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_scrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    m_scrollArea->setMinimumSize(0, 0);
    m_scrollArea->setFocusPolicy(Qt::NoFocus);
    m_scrollArea->viewport()->setFocusPolicy(Qt::NoFocus);
    m_scrollArea->viewport()->setMinimumSize(0, 0);
    m_scrollArea->setStyleSheet(
        "QScrollArea { background-color: #1a1a2e; }"
        "QScrollBar:vertical { background: #2a2a3e; width: 10px; }"
        "QScrollBar:horizontal { background: #2a2a3e; height: 10px; }"
        "QScrollBar::handle { background: #555577; border-radius: 4px; }"
        "QScrollBar::handle:hover { background: #7777aa; }"
        "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }"
    );
    m_scrollArea->setGeometry(rect());
    m_scrollArea->viewport()->setStyleSheet("background-color: #1a1a2e;");

    // QtのネイティブビューにVST3 GUIを直接アタッチする。
    m_container = new QWidget();
    m_container->setAttribute(Qt::WA_NativeWindow);
    m_container->setAttribute(Qt::WA_InputMethodEnabled);
    m_container->setAttribute(Qt::WA_NoMousePropagation);
    m_container->setFocusPolicy(Qt::StrongFocus);
    m_container->setMouseTracking(true);
    m_container->setFixedSize(logicalW, logicalH);
    setFocusProxy(m_container);

    m_scrollArea->setWidget(m_container);
    m_scrollArea->show();
    m_container->show();

    m_scaleFactor = 1.0;
    setPluginInputPriority(true);

#ifdef Q_OS_MAC
    syncMacNativeClipState(m_scrollArea, m_container);
#endif

    void* nativeHostView = reinterpret_cast<void*>(m_container->winId());
    tresult result = m_plugView->attached(nativeHostView, platformType);
    if (result != kResultTrue) {
        qWarning() << "PluginEditor: attached() failed:" << result;
        m_plugView->release();
        m_plugView = nullptr;
        setPluginInputPriority(false);
        setFocusProxy(nullptr);
        delete m_scrollArea;
        m_scrollArea = nullptr;
        m_container = nullptr;
        m_nativeWidth = 0;
        m_nativeHeight = 0;
        m_scaleFactor = 1.0;
        m_supportsResize = false;
        return false;
    }

    qDebug() << "PluginEditor: Attached successfully (native:" << m_nativeWidth << "x" << m_nativeHeight << ")";

#ifdef Q_OS_MAC
    // Cocoa 側の responder chain に乗りやすくするため、アタッチ直後にホストへフォーカスを寄せる。
    m_container->setFocus(Qt::OtherFocusReason);
    m_plugView->onFocus(true);
    syncMacNativeClipState(m_scrollArea, m_container);

    // スクロール・レイアウト更新後にも clip を再同期し、native view のはみ出しを抑える。
    connect(m_scrollArea->horizontalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { syncMacNativeClipState(m_scrollArea, m_container); });
    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { syncMacNativeClipState(m_scrollArea, m_container); });
    QTimer::singleShot(0, this, [this]() {
        syncMacNativeClipState(m_scrollArea, m_container);
    });
#endif

    // 初回スケール計算
    updateScaleMode();
    scheduleDeferredViewSync();
    return true;
#else
    qWarning() << "PluginEditor: このOSは未対応";
    m_plugView->release();
    m_plugView = nullptr;
    return false;
#endif
}

// 古いonScaleChangedは削除済み

void PluginEditorWidget::enterDirectMode()
{
#ifdef Q_OS_WIN
    if (!m_bitmapCaptureActive) return; // 既にダイレクトモード

    qDebug() << "PluginEditor: ダイレクトモードに切替";
    m_captureTimer.stop();

    // プラグインを現在のHWNDからデタッチ
    m_plugView->removed();

    // スケーリング表示を削除
    if (m_scaledDisplay) {
        delete m_scaledDisplay;
        m_scaledDisplay = nullptr;
    }

    // デバイスピクセル → Qt論理ピクセル変換してコンテナサイズ設定
    double dpr = getDevicePixelRatio(this);
    int logicalW = qMax(1, static_cast<int>(m_nativeWidth / dpr));
    int logicalH = qMax(1, static_cast<int>(m_nativeHeight / dpr));

    if (m_scrollArea) {
        // コンテナをスクロールエリアに戻す
        m_container->setAttribute(Qt::WA_NativeWindow);
        m_container->setFixedSize(logicalW, logicalH);
        m_scrollArea->setWidget(m_container);
        m_container->show();
    } else {
        // フォールバック: このウィジェットの直接の子として配置
        m_container->setParent(this);
        m_container->setAttribute(Qt::WA_NativeWindow);
        m_container->setFixedSize(logicalW, logicalH);
        m_container->show();
    }

    // プラグインを新しいHWNDに再アタッチ
    HWND hwnd = reinterpret_cast<HWND>(m_container->winId());
    tresult result = m_plugView->attached(reinterpret_cast<void*>(hwnd), kPlatformTypeHWND);
    if (result != kResultTrue) {
        qWarning() << "PluginEditor: ダイレクトモードへの再アタッチ失敗:" << result;
    }

    // オフスクリーンウィンドウを削除
    delete m_offscreenHost;
    m_offscreenHost = nullptr;

    m_bitmapCaptureActive = false;
#endif
}

void PluginEditorWidget::enterBitmapCaptureMode()
{
#ifdef Q_OS_WIN
    if (m_bitmapCaptureActive) {
        return; // サイズと位置はupdateScaleModeで管理
    }

    qDebug() << "PluginEditor: ビットマップキャプチャモードに切替";

    // プラグインを現在のHWNDからデタッチ
    m_plugView->removed();

    // デバイスピクセル → Qt論理ピクセル変換
    double dpr = getDevicePixelRatio(this);
    int logicalW = qMax(1, static_cast<int>(m_nativeWidth / dpr));
    int logicalH = qMax(1, static_cast<int>(m_nativeHeight / dpr));

    // オフスクリーンのトップレベルウィンドウを作成
    m_offscreenHost = new QWidget(nullptr,
        Qt::Window | Qt::FramelessWindowHint | Qt::Tool);
    m_offscreenHost->setAttribute(Qt::WA_ShowWithoutActivating);
    m_offscreenHost->setFixedSize(logicalW, logicalH);
    m_offscreenHost->move(-20000, -20000);

    // コンテナをオフスクリーンウィンドウの子に移動
    m_container->setParent(m_offscreenHost);
    m_container->setAttribute(Qt::WA_NativeWindow);
    m_container->setFixedSize(logicalW, logicalH);
    m_container->move(0, 0);

    m_offscreenHost->show();
    m_container->show();

    // プラグインを新しいHWNDに再アタッチ
    HWND hwnd = reinterpret_cast<HWND>(m_container->winId());
    tresult result = m_plugView->attached(reinterpret_cast<void*>(hwnd), kPlatformTypeHWND);
    if (result != kResultTrue) {
        qWarning() << "PluginEditor: ビットマップキャプチャモードへの再アタッチ失敗:" << result;
    }

    // スケーリング表示ウィジェットを作成（サイズと位置はupdateScaleModeで設定）
    m_scaledDisplay = new ScaledPluginDisplay(this);
    m_scaledDisplay->setPluginHwnd(reinterpret_cast<void*>(hwnd));
    m_scaledDisplay->setScaleFactor(m_scaleFactor);
    m_scaledDisplay->setNativeSize(m_nativeWidth, m_nativeHeight);
    m_scaledDisplay->show();

    // ユーザー操作時の即時更新を接続
    connect(m_scaledDisplay, &ScaledPluginDisplay::userInteracted,
            this, [this]() {
        QTimer::singleShot(16, this, &PluginEditorWidget::capturePluginView);
    });

    // 定期キャプチャ開始（約30fps）
    m_captureTimer.start(33);
    m_bitmapCaptureActive = true;
    setPluginInputPriority(true);

    // 初回キャプチャ
    QTimer::singleShot(100, this, &PluginEditorWidget::capturePluginView);
#endif
}

void PluginEditorWidget::capturePluginView()
{
#ifdef Q_OS_WIN
    if (!m_container || !m_scaledDisplay || !m_bitmapCaptureActive) return;

    HWND hwnd = reinterpret_cast<HWND>(m_container->winId());
    if (!hwnd) return;

    int w = m_nativeWidth;
    int h = m_nativeHeight;
    if (w <= 0 || h <= 0) return;

    // プラグインウィンドウをキャプチャ
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, w, h);
    HGDIOBJ hOld = SelectObject(hdcMem, hBitmap);

    // PW_RENDERFULLCONTENT: DirectX/OpenGLコンテンツも含めてキャプチャ（Win8.1+）
    BOOL captured = PrintWindow(hwnd, hdcMem, PW_RENDERFULLCONTENT);
    if (captured) {
        // HBITMAPをQImageに変換
        BITMAPINFOHEADER bi = {};
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = w;
        bi.biHeight = -h; // トップダウン（上→下）
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;

        QImage img(w, h, QImage::Format_ARGB32);
        GetDIBits(hdcMem, hBitmap, 0, h, img.bits(),
                  reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

        // DPI補正済みの論理ネイティブサイズにスケール倍率を掛けて表示サイズを計算
        double dpr = getDevicePixelRatio(this);
        double logicalW = w / dpr;
        double logicalH = h / dpr;
        int scaledW = qMax(1, static_cast<int>(logicalW * m_scaleFactor));
        int scaledH = qMax(1, static_cast<int>(logicalH * m_scaleFactor));
        QPixmap pixmap = QPixmap::fromImage(img).scaled(
            scaledW, scaledH, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        m_scaledDisplay->setPixmap(pixmap);
    }

    // GDIリソース解放
    SelectObject(hdcMem, hOld);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
#endif
}

void PluginEditorWidget::closeEditor()
{
    m_captureTimer.stop();
    setPluginInputPriority(false);
    setFocusProxy(nullptr);
    releaseMouse();
    clearFocus();

    if (m_scaledDisplay) {
        m_scaledDisplay->releaseMouse();
        m_scaledDisplay->hide();
    }
    if (m_container) {
        m_container->releaseMouse();
        m_container->clearFocus();
        m_container->hide();
    }
    if (m_scrollArea) {
        if (QWidget* viewport = m_scrollArea->viewport()) {
            viewport->releaseMouse();
            viewport->clearFocus();
        }
        m_scrollArea->releaseMouse();
        m_scrollArea->clearFocus();
        m_scrollArea->hide();
    }

    if (m_plugView) {
#ifdef Q_OS_MAC
        m_plugView->onFocus(false);
#endif
        m_plugView->removed();
#ifdef Q_OS_MAC
        if (m_container) {
            // detach 後に残留した native subview が splitter 操作へ触らないようにする。
            Darwin::clearMacHostedSubviews(m_container);
        }
#endif
        m_plugView->release();
        m_plugView = nullptr;
    }
#ifdef Q_OS_MAC
    if (m_container) {
        // エディタを閉じた後に親NSViewへ残る状態を戻し、ウィンドウ操作へ副作用を残さない。
        Darwin::clearMacHostedSubviews(m_container);
        Darwin::clearMacClipWidget(m_container);
    }
#endif
    m_plugFrame = nullptr;

    // PlugFrameの解放
    if (s_currentPlugFrame) {
        delete s_currentPlugFrame;
        s_currentPlugFrame = nullptr;
    }

    // setWidget() 所有のまま scroll area を消すと片付け順を制御しづらいので、
    // 先にコンテナを切り離してから順番に破棄する。
    if (m_scrollArea && m_container && m_scrollArea->widget() == m_container) {
        m_scrollArea->takeWidget();
    }

    if (m_offscreenHost) {
        delete m_offscreenHost;
        m_offscreenHost = nullptr;
        m_container = nullptr;
    }

    if (m_scrollArea) {
        delete m_scrollArea;
        m_scrollArea = nullptr;
    }

    if (m_container) {
        delete m_container;
        m_container = nullptr;
    }

    // スケーリング表示の削除
    if (m_scaledDisplay) {
        delete m_scaledDisplay;
        m_scaledDisplay = nullptr;
    }

    if (m_scaleLabel) {
        m_scaleLabel->hide();
    }

    m_nativeWidth = 0;
    m_nativeHeight = 0;
    m_scaleFactor = 1.0;
    m_supportsResize = false;
    m_bitmapCaptureActive = false;
    m_deferredViewSyncPending = false;

    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
}
