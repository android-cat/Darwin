#include "FontManager.h"

#include <QFont>
#include <QFontDatabase>
#include <QStringList>

namespace Darwin {

namespace {

QString systemUiFamily()
{
    const QFont systemFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (!systemFont.family().isEmpty()) {
        return systemFont.family();
    }
    return QStringLiteral("Arial");
}

QString systemMonoFamily()
{
    const QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!fixedFont.family().isEmpty()) {
        return fixedFont.family();
    }
    return QStringLiteral("Courier New");
}

bool hasFamily(const QString& family)
{
    return QFontDatabase::families().contains(family, Qt::CaseInsensitive);
}

QString firstAvailableFamily(const QStringList& candidates, const QString& fallback)
{
    for (const QString& candidate : candidates) {
        if (hasFamily(candidate)) {
            return candidate;
        }
    }
    return fallback;
}

QString resolveUiFamily()
{
#ifdef Q_OS_MAC
    return systemUiFamily();
#elif defined(Q_OS_WIN)
    return firstAvailableFamily({QStringLiteral("Segoe UI")}, systemUiFamily());
#else
    return firstAvailableFamily({QStringLiteral("Arial")}, systemUiFamily());
#endif
}

QString resolveMonoFamily()
{
#ifdef Q_OS_MAC
    return systemMonoFamily();
#elif defined(Q_OS_WIN)
    return firstAvailableFamily(
        {QStringLiteral("Roboto Mono"), QStringLiteral("Consolas"), QStringLiteral("Courier New")},
        systemMonoFamily());
#else
    return firstAvailableFamily({QStringLiteral("Roboto Mono"), QStringLiteral("Courier New")},
                                systemMonoFamily());
#endif
}

void insertAlias(const QString& sourceFamily, const QString& targetFamily)
{
    if (sourceFamily.isEmpty() || targetFamily.isEmpty()) {
        return;
    }

    // 同じエイリアスを重複登録しないようにしておく
    if (QFont::substitutes(sourceFamily).contains(targetFamily)) {
        return;
    }

    QFont::insertSubstitution(sourceFamily, targetFamily);
}

QString toCssFamily(const QString& family, const QString& genericFamily)
{
    return QString("'%1', %2").arg(family, genericFamily);
}

} // namespace

void FontManager::configureApplicationFonts(QApplication& app)
{
    const QString uiFamily = resolveUiFamily();
    const QString monoFamily = resolveMonoFamily();

    // 既存コードの移行途中でも見た目が崩れにくいよう、旧来フォント名を共通定義へ寄せる
    insertAlias(QStringLiteral("Arial"), uiFamily);
    insertAlias(QStringLiteral("Helvetica Neue"), uiFamily);
    insertAlias(QStringLiteral("Segoe UI"), uiFamily);
    insertAlias(QStringLiteral("Segoe UI Light"), uiFamily);
    insertAlias(QStringLiteral("Roboto Mono"), monoFamily);

    QFont appFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    appFont.setFamily(uiFamily);
    appFont.setStyleHint(QFont::SansSerif);
    app.setFont(appFont);
}

QString FontManager::uiFontFamily()
{
    static const QString s_uiFamily = resolveUiFamily();
    return s_uiFamily;
}

QString FontManager::monoFontFamily()
{
    static const QString s_monoFamily = resolveMonoFamily();
    return s_monoFamily;
}

QString FontManager::uiFontCss()
{
    return toCssFamily(uiFontFamily(), QStringLiteral("sans-serif"));
}

QString FontManager::monoFontCss()
{
    return toCssFamily(monoFontFamily(), QStringLiteral("monospace"));
}

QFont FontManager::uiFont(int pointSize, int weight)
{
    QFont font(uiFontFamily(), pointSize, weight);
    font.setStyleHint(QFont::SansSerif);
    return font;
}

QFont FontManager::monoFont(int pointSize, int weight)
{
    QFont font(monoFontFamily(), pointSize, weight);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
}

} // namespace Darwin
