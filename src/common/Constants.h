#pragma once

/**
 * @brief プロジェクト全体で共有されるタイミング・レイアウト定数
 *
 * 各ビュー / ウィジェットが独自に宣言していた重複定数をここに集約する。
 * 使用側では  #include "common/Constants.h"  して
 *   Darwin::TICKS_PER_BEAT  等の形でアクセスする。
 */
namespace Darwin {

// ───── タイミング定数 ─────
constexpr int    TICKS_PER_BEAT  = 480;
constexpr int    BEATS_PER_BAR   = 4;
constexpr int    TICKS_PER_BAR   = TICKS_PER_BEAT * BEATS_PER_BAR; // 1920
constexpr double PIXELS_PER_BAR  = 100.0;                          // デフォルト 1小節 = 100px
constexpr double PIXELS_PER_TICK = PIXELS_PER_BAR / TICKS_PER_BAR; // ~0.052

// ───── 共通UI定数 ─────
constexpr char   ACCENT_COLOR_HEX[]       = "#FF3366"; // プロジェクト標準アクセント
constexpr int    UI_ANIMATION_INTERVAL_MS = 16;        // 60fps 相当
constexpr float  PLAYHEAD_TRAIL_FADE_STEP = 0.12f;
constexpr double PLAYHEAD_TRAIL_LENGTH    = 80.0;      // 再生ヘッドの光の尾の長さ(px)

// ───── Arrangement固有 ─────
constexpr int    ARRANGEMENT_TRACK_ROW_HEIGHT = 100;
constexpr int    ARRANGEMENT_HEADER_WIDTH     = 200;
constexpr int    ARRANGEMENT_TIMELINE_HEIGHT  = 40;

// ───── ピアノロール固有 ─────
constexpr int    ROW_HEIGHT      = 12;
constexpr int    NUM_ROWS        = 128;   // MIDI 0–127
constexpr int    TOTAL_BARS      = 64;

} // namespace Darwin
