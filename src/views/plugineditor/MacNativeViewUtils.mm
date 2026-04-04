#include "MacNativeViewUtils.h"

#include <QWidget>

#ifdef Q_OS_MAC
#import <AppKit/AppKit.h>
#import <QuartzCore/CALayer.h>

namespace Darwin {
namespace {

constexpr NSInteger kMaxClipSuperviewDepth = 6;

void refreshView(NSView* view);

NSView* nativeViewForWidget(QWidget* widget)
{
    if (!widget) {
        return nil;
    }

    const void* nativeHandle = reinterpret_cast<const void*>(widget->winId());
    return (__bridge NSView*)nativeHandle;
}

void synchronizeHostedSubviews(NSView* hostView)
{
    if (!hostView) {
        return;
    }

    // 埋め込んだプラグイン側 NSView がホスト bounds に追従するよう、
    // 直下 subview の autoresize と frame を毎回揃える。
    const NSRect hostBounds = [hostView bounds];
    for (NSView* childView in [hostView subviews]) {
        if (!childView) {
            continue;
        }

        [childView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
        [childView setAutoresizesSubviews:YES];
        if (!NSEqualRects([childView frame], hostBounds)) {
            [childView setFrame:hostBounds];
        }
        refreshView(childView);
    }

    refreshView(hostView);
}

void applyClipToView(NSView* view)
{
    if (!view) {
        return;
    }

    // layer を持たせ、native child を bounds 内に閉じ込める。
    [view setWantsLayer:YES];
    [view setAutoresizesSubviews:YES];

    CALayer* layer = [view layer];
    if (layer) {
        layer.masksToBounds = YES;
        layer.needsDisplayOnBoundsChange = YES;
    }
}

void refreshView(NSView* view)
{
    if (!view) {
        return;
    }

    [view setNeedsLayout:YES];
    [view setNeedsDisplay:YES];
    if ([view respondsToSelector:@selector(layoutSubtreeIfNeeded)]) {
        [view layoutSubtreeIfNeeded];
    }
}

void clearClipFromView(NSView* view)
{
    if (!view) {
        return;
    }

    CALayer* layer = [view layer];
    if (layer) {
        layer.masksToBounds = NO;
        layer.needsDisplayOnBoundsChange = NO;
    }
}

void removeHostedSubviews(NSView* hostView)
{
    if (!hostView) {
        return;
    }

    // removed() 後も直下 subview が残るプラグインがあるため、
    // ホスト破棄前に明示的に外して splitter 操作への干渉を防ぐ。
    while ([[hostView subviews] count] > 0) {
        NSView* childView = [[hostView subviews] lastObject];
        if (!childView) {
            break;
        }

        [childView setHidden:YES];
        [childView removeFromSuperviewWithoutNeedingDisplay];
    }

    refreshView(hostView);
}

} // namespace

void prepareMacClipWidget(QWidget* widget)
{
    if (!widget) {
        return;
    }

    widget->setAttribute(Qt::WA_NativeWindow);
    widget->winId();

    synchronizeMacHostedSubviewGeometry(widget);
    synchronizeMacClipWidget(widget);
}

void synchronizeMacClipWidget(QWidget* widget)
{
    synchronizeMacHostedSubviewGeometry(widget);

    NSInteger depth = 0;
    for (NSView* view = nativeViewForWidget(widget);
         view != nil && depth < kMaxClipSuperviewDepth;
         view = [view superview], ++depth) {
        applyClipToView(view);
        refreshView(view);
    }
}

void synchronizeMacHostedSubviewGeometry(QWidget* widget)
{
    synchronizeHostedSubviews(nativeViewForWidget(widget));
}

void clearMacHostedSubviews(QWidget* widget)
{
    removeHostedSubviews(nativeViewForWidget(widget));
}

void clearMacClipWidget(QWidget* widget)
{
    NSInteger depth = 0;
    for (NSView* view = nativeViewForWidget(widget);
         view != nil && depth < kMaxClipSuperviewDepth;
         view = [view superview], ++depth) {
        clearClipFromView(view);
        refreshView(view);
    }
}

} // namespace Darwin

#else

namespace Darwin {

void prepareMacClipWidget(QWidget* widget)
{
    Q_UNUSED(widget);
}

void synchronizeMacClipWidget(QWidget* widget)
{
    Q_UNUSED(widget);
}

void clearMacHostedSubviews(QWidget* widget)
{
    Q_UNUSED(widget);
}

void clearMacClipWidget(QWidget* widget)
{
    Q_UNUSED(widget);
}

} // namespace Darwin

#endif
