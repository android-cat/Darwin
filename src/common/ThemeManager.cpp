#include "ThemeManager.h"
#include "Constants.h"

namespace Darwin {

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent), m_isDarkMode(true) // デフォルトはダーク
{
}

void ThemeManager::initialize()
{
    // OS のカラーテーマ（ライト/ダーク）設定を取得
    const auto scheme = qApp->styleHints()->colorScheme();
    m_isDarkMode = (scheme == Qt::ColorScheme::Dark);

    // デフォルトで Dark に落ち着くことが多いが、OS が明示的に Light だった場合に対応
    emit themeChanged();
}

void ThemeManager::toggleTheme()
{
    m_isDarkMode = !m_isDarkMode;
    emit themeChanged();
}

// -------------------------------------------------------------
// 色定義 (Dark / Light)
// -------------------------------------------------------------

QColor ThemeManager::backgroundColor() const
{
    return m_isDarkMode ? QColor("#1e1e1e") : QColor("#ffffff");
}

QColor ThemeManager::textColor() const
{
    return m_isDarkMode ? QColor("#e2e8f0") : QColor("#1e293b");
}

QColor ThemeManager::secondaryTextColor() const
{
    return m_isDarkMode ? QColor("#94a3b8") : QColor("#64748b");
}

QColor ThemeManager::gridLineColor() const
{
    // 小節線（一番濃い）
    return m_isDarkMode ? QColor("#334155") : QColor("#e2e8f0");
}

QColor ThemeManager::gridLineSubColor() const
{
    // 拍線（4分音符）
    return m_isDarkMode ? QColor("#1e293b") : QColor("#f1f5f9");
}

QColor ThemeManager::gridLineSubBeatColor() const
{
    // 8分音符線
    return m_isDarkMode ? QColor("#222f42") : QColor("#f5f7fa");
}

QColor ThemeManager::gridLineTickColor() const
{
    // 16/32分音符線（最も薄い）
    return m_isDarkMode ? QColor(255, 255, 255, 12) : QColor(0, 0, 0, 12);
}

QColor ThemeManager::pianoBlackKeyColor() const
{
    // ピアノの黒鍵
    return m_isDarkMode ? QColor("#1e1e1e") : QColor("#e2e8f0");
}

QColor ThemeManager::pianoWhiteKeyColor() const
{
    // ピアノの白鍵
    return m_isDarkMode ? QColor("#f8fafc") : QColor("#f8fafc");
}

QColor ThemeManager::panelBackgroundColor() const
{
    // ミキサーやその他のパネルの背景
    return m_isDarkMode ? QColor("#252526") : QColor("#f8fafc");
}

QColor ThemeManager::borderColor() const
{
    // ボーダーや区切り線
    return m_isDarkMode ? QColor("#334155") : QColor("#cbd5e1");
}

QColor ThemeManager::accentColor() const
{
    return QColor(ACCENT_COLOR_HEX); // プロジェクト標準のピンク系アクセント
}

QColor ThemeManager::accentBgColor() const
{
    // FXボタンなどの背景
    return m_isDarkMode ? QColor("#4a0e1c") : QColor("#ffe4e6");
}

QColor ThemeManager::accentBorderColor() const
{
    // FXボタンなどの境界
    return m_isDarkMode ? QColor("#881c34") : QColor("#fb7185");
}

QColor ThemeManager::accentTextColor() const
{
    // アクセント背景上のテキスト
    return m_isDarkMode ? QColor("#fecdd3") : QColor("#e11d48");
}

QColor ThemeManager::successColor() const
{
    return QColor("#22c55e"); // green-500
}

QColor ThemeManager::errorColor() const
{
    return QColor("#ef4444"); // red-500
}

} // namespace Darwin
