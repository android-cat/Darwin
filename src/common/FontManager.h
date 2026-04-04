#pragma once

#include <QApplication>
#include <QFont>
#include <QString>

namespace Darwin {

class FontManager
{
public:
    static void configureApplicationFonts(QApplication& app);
    static QString uiFontFamily();
    static QString monoFontFamily();
    static QString uiFontCss();
    static QString monoFontCss();
    static QFont uiFont(int pointSize, int weight = QFont::Normal);
    static QFont monoFont(int pointSize, int weight = QFont::Normal);
};

} // namespace Darwin
