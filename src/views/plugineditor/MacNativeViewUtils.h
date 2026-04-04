#pragma once

class QWidget;

namespace Darwin {

// macOS の NSView ホストを native 化し、clip 用の下準備を行う。
void prepareMacClipWidget(QWidget* widget);

// macOS の NSView / CALayer に clip 更新を伝え、スクロール後のはみ出しを抑える。
void synchronizeMacClipWidget(QWidget* widget);

// macOS の埋め込み NSView をホスト bounds へ揃え、リサイズ追従を安定させる。
void synchronizeMacHostedSubviewGeometry(QWidget* widget);

// macOS のホスト直下に残った埋め込み NSView を明示的に外す。
void clearMacHostedSubviews(QWidget* widget);

// macOS の clip 設定を後始末し、エディタクローズ後に状態が残らないようにする。
void clearMacClipWidget(QWidget* widget);

} // namespace Darwin
