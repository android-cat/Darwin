#pragma once

#include <QObject>
#include <QColor>
#include <QIcon>
#include <QApplication>
#include <QStyleHints>

namespace Darwin {

class ThemeManager : public QObject {
    Q_OBJECT

public:
    static ThemeManager& instance() {
        static ThemeManager s_instance;
        return s_instance;
    }

    // テーマの初期化（OSの設定読み取り等）
    void initialize();

    // テーマ手動切り替え
    void toggleTheme();

    // 現在の状態取得
    bool isDarkMode() const { return m_isDarkMode; }

    // 主なカラーパレット提供メソッド群
    QColor backgroundColor() const;
    QColor textColor() const;
    QColor secondaryTextColor() const;
    QColor gridLineColor() const;
    QColor gridLineSubColor() const;
    QColor gridLineSubBeatColor() const;
    QColor gridLineTickColor() const;
    
    QColor pianoBlackKeyColor() const;
    QColor pianoWhiteKeyColor() const;
    QColor panelBackgroundColor() const;
    QColor borderColor() const;

    // アクセントカラー関連
    QColor accentColor() const;
    QColor accentBgColor() const;
    QColor accentBorderColor() const;
    QColor accentTextColor() const;

    // セマンティックカラー
    QColor successColor() const;
    QColor errorColor() const;

signals:
    // テーマ切り替え時に全ビューへ通知されるシグナル
    void themeChanged();

private:
    ThemeManager(QObject* parent = nullptr);
    ~ThemeManager() = default;

    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    bool m_isDarkMode;
};

} // namespace Darwin
