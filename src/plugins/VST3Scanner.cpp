#include "VST3Scanner.h"
#include "VST3MetadataProbe.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QSettings>
#include <QSet>
#include <cstring>
#include <exception>
#ifdef Q_OS_WIN
#define SMTG_OS_WINDOWS 1
#include <windows.h>
#endif

#ifndef STDMETHODCALLTYPE
#ifdef Q_OS_WIN
#define STDMETHODCALLTYPE __stdcall
#else
#define STDMETHODCALLTYPE
#endif
#endif

#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
QString normalizeScanPath(const QString& path)
{
    return QDir::fromNativeSeparators(path.trimmed());
}

QStringList normalizeScanPaths(const QStringList& paths)
{
    QStringList normalized;
    for (const QString& path : paths) {
        const QString cleaned = normalizeScanPath(path);
        if (cleaned.isEmpty()) {
            continue;
        }
        if (!normalized.contains(cleaned, Qt::CaseInsensitive)) {
            normalized << cleaned;
        }
    }
    return normalized;
}

void appendUniquePath(QStringList& paths, const QString& path)
{
    const QString cleaned = normalizeScanPath(path);
    if (cleaned.isEmpty()) {
        return;
    }
    if (!paths.contains(cleaned, Qt::CaseInsensitive)) {
        paths << cleaned;
    }
}

QString environmentPath(const char* variableName)
{
    return normalizeScanPath(qEnvironmentVariable(variableName).trimmed());
}

QString applicationSpecificVst3Path()
{
#ifdef Q_OS_WIN
    const QString appDir = normalizeScanPath(QCoreApplication::applicationDirPath());
    if (appDir.isEmpty()) {
        return {};
    }
    return normalizeScanPath(QDir(appDir).filePath(QStringLiteral("VST3")));
#elif defined(Q_OS_MAC)
    const QString appDir = normalizeScanPath(QCoreApplication::applicationDirPath());
    if (appDir.isEmpty()) {
        return {};
    }

    QDir appDirHandle(appDir);
    if (appDirHandle.dirName() != QStringLiteral("MacOS") || !appDirHandle.cdUp()) {
        return {};
    }
    return normalizeScanPath(appDirHandle.filePath(QStringLiteral("VST3")));
#else
    return {};
#endif
}

#ifdef Q_OS_WIN
QStringList preferredWindowsArchitectureDirs()
{
    QStringList directories;
#if defined(_M_ARM64)
    directories << QStringLiteral("arm64x-win")
                << QStringLiteral("arm64ec-win")
                << QStringLiteral("arm64-win")
                << QStringLiteral("x86_64-win")
                << QStringLiteral("x86-win");
#elif defined(_M_X64)
    directories << QStringLiteral("x86_64-win")
                << QStringLiteral("arm64x-win")
                << QStringLiteral("arm64ec-win")
                << QStringLiteral("arm64-win")
                << QStringLiteral("x86-win");
#elif defined(_M_IX86)
    directories << QStringLiteral("x86-win")
                << QStringLiteral("x86_64-win")
                << QStringLiteral("arm64x-win")
                << QStringLiteral("arm64ec-win")
                << QStringLiteral("arm64-win");
#else
    directories << QStringLiteral("x86_64-win")
                << QStringLiteral("arm64x-win")
                << QStringLiteral("arm64ec-win")
                << QStringLiteral("arm64-win")
                << QStringLiteral("x86-win");
#endif
    return directories;
}

QStringList windowsBundleBinaryCandidates(const QString& bundlePath, const QString& bundleName)
{
    QStringList candidates;
    const QDir contentsDir(bundlePath + QStringLiteral("/Contents"));
    if (!contentsDir.exists()) {
        return candidates;
    }

    auto appendArchCandidates = [&](const QString& architectureDirName) {
        const QDir archDir(contentsDir.filePath(architectureDirName));
        if (!archDir.exists()) {
            return;
        }

        appendUniquePath(candidates, archDir.filePath(bundleName + QStringLiteral(".vst3")));

        const QStringList entries =
            archDir.entryList(QStringList() << QStringLiteral("*.vst3"), QDir::Files);
        for (const QString& entry : entries) {
            appendUniquePath(candidates, archDir.absoluteFilePath(entry));
        }
    };

    for (const QString& preferredDir : preferredWindowsArchitectureDirs()) {
        appendArchCandidates(preferredDir);
    }

    const QStringList dynamicDirs =
        contentsDir.entryList(QStringList() << QStringLiteral("*-win"), QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& architectureDirName : dynamicDirs) {
        appendArchCandidates(architectureDirName);
    }

    return candidates;
}
#endif

QString storedScanPathsKey()
{
    return QStringLiteral("VST3Scanner/ScanPaths");
}

QStringList loadStoredScanPaths()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("Darwin"), QStringLiteral("PluginUsage"));
    return normalizeScanPaths(settings.value(storedScanPathsKey()).toStringList());
}

void saveStoredScanPaths(const QStringList& paths)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("Darwin"), QStringLiteral("PluginUsage"));
    settings.setValue(storedScanPathsKey(), normalizeScanPaths(paths));
}

QString pluginIdentityPath(const QString& path)
{
    const QFileInfo info(path);
    const QString canonicalPath = info.canonicalFilePath();
    if (!canonicalPath.isEmpty()) {
        return canonicalPath;
    }
    return info.absoluteFilePath();
}

bool isAudioModuleCategory(const char* categoryText)
{
    return categoryText != nullptr && std::strcmp(categoryText, kVstAudioEffectClass) == 0;
}

bool isAudioModuleCategory(const QString& categoryText)
{
    return categoryText == QString::fromLatin1(kVstAudioEffectClass);
}

QString jsonStringByKeys(const QJsonObject& object, const QStringList& keys)
{
    for (const QString& key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isString()) {
            const QString text = value.toString().trimmed();
            if (!text.isEmpty()) {
                return text;
            }
        }
    }
    return {};
}

QString jsonStringList(const QJsonValue& value)
{
    if (value.isString()) {
        return value.toString().trimmed();
    }
    if (!value.isArray()) {
        return {};
    }

    QStringList parts;
    for (const QJsonValue& item : value.toArray()) {
        if (item.isString()) {
            const QString text = item.toString().trimmed();
            if (!text.isEmpty()) {
                parts << text;
            }
        }
    }
    return parts.join(QStringLiteral(", "));
}

void classifyPlugin(const QString& categoryText, const QString& subCategoryText, VST3PluginInfo& info)
{
    const QString combined = (categoryText + QStringLiteral(" ") + subCategoryText).trimmed();

    const bool isInstrument =
        combined.contains(QStringLiteral("Instrument"), Qt::CaseInsensitive) ||
        combined.contains(QStringLiteral("Synth"), Qt::CaseInsensitive) ||
        combined.contains(QStringLiteral("Sampler"), Qt::CaseInsensitive);
    const bool isEffect =
        combined.contains(QStringLiteral("Fx"), Qt::CaseInsensitive) ||
        combined.contains(QStringLiteral("Effect"), Qt::CaseInsensitive);

    if (isInstrument) {
        info.isInstrument = true;
        if (info.category.isEmpty()) {
            info.category = QStringLiteral("Instrument");
        }
    }
    if (isEffect) {
        info.isEffect = true;
        if (info.category.isEmpty() || !info.isInstrument) {
            info.category = QStringLiteral("Fx");
        }
    }

    // 情報が曖昧なプラグインでも一覧から消えないよう、最低限エフェクト扱いに寄せる。
    if (!info.isInstrument && !info.isEffect) {
        info.isEffect = true;
        if (info.category.isEmpty()) {
            info.category = categoryText.isEmpty() ? QStringLiteral("Fx") : categoryText;
        }
    }
}

#ifdef Q_OS_MAC
struct BundlePlistMetadata {
    QString shortVersion;
    QString bundleVersion;
    QString bundleIdentifier;
};

QString normalizeIdentifierWords(const QString& text)
{
    QString normalized = text.trimmed();
    normalized.replace(QLatin1Char('-'), QLatin1Char(' '));
    normalized.replace(QLatin1Char('_'), QLatin1Char(' '));

    const QStringList words = normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        return {};
    }

    QStringList result;
    for (QString word : words) {
        if (word == word.toLower()) {
            word[0] = word[0].toUpper();
        }
        result << word;
    }
    return result.join(QLatin1Char(' '));
}

QString inferVendorFromBundleIdentifier(const QString& bundleIdentifier)
{
    const QString trimmed = bundleIdentifier.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QStringList parts = trimmed.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (parts.size() >= 2) {
        const QString vendor = normalizeIdentifierWords(parts.at(1));
        if (!vendor.isEmpty()) {
            return vendor;
        }
    }

    return normalizeIdentifierWords(trimmed);
}

BundlePlistMetadata readBundlePlistMetadata(const QString& bundlePath)
{
    const QString plistPath = bundlePath + QStringLiteral("/Contents/Info.plist");
    if (!QFile::exists(plistPath)) {
        return {};
    }

    QSettings settings(plistPath, QSettings::NativeFormat);
    BundlePlistMetadata metadata;
    metadata.shortVersion =
        settings.value(QStringLiteral("CFBundleShortVersionString")).toString().trimmed();
    metadata.bundleVersion =
        settings.value(QStringLiteral("CFBundleVersion")).toString().trimmed();
    metadata.bundleIdentifier =
        settings.value(QStringLiteral("CFBundleIdentifier")).toString().trimmed();
    return metadata;
}

bool populateVersionFromBundlePlist(const BundlePlistMetadata& metadata, VST3PluginInfo& info)
{
    // まずユーザー向けの短いバージョン表記を優先し、無い時だけ build 番号に寄せる。
    if (info.version.isEmpty()) {
        const QString candidate = metadata.shortVersion.isEmpty()
            ? metadata.bundleVersion
            : metadata.shortVersion;
        if (!candidate.isEmpty()) {
            info.version = candidate;
            return true;
        }
    }
    return false;
}

bool populateVendorFromBundleIdentifier(const BundlePlistMetadata& metadata, VST3PluginInfo& info)
{
    // bundle identifier 由来の vendor は推定値なので、本当に空の時だけ補完する。
    if (info.vendor.isEmpty()) {
        const QString inferredVendor = inferVendorFromBundleIdentifier(metadata.bundleIdentifier);
        if (!inferredVendor.isEmpty()) {
            info.vendor = inferredVendor;
            return true;
        }
    }
    return false;
}
#endif
}

// ============================================================
// VST3 DLL 安全ロードヘルパー
//
// 【MSVC ビルド】
//   Windows SEH (__try/__except) でアクセス違反を捕捉する。
//   __try ブロック内に C++ デストラクタを持つオブジェクトは置けないため、
//   POD 型・raw ポインタのみ使用し、COM インタフェースを直接操作する。
//
// 【MinGW/GCC ビルド】
//   SEH が利用できないため QLibrary + try/catch(...) を使用する。
//   ハードウェア例外（アクセス違反）の捕捉は不完全だが、
//   C++ 例外・ロード失敗は安全にハンドリングする。
// ============================================================

#ifdef Q_OS_WIN
#ifdef _MSC_VER
// ──────────────────────────────────────────
// MSVC: SEH 保護付きローダー
// ──────────────────────────────────────────

/** DLL から抽出したプラグイン生情報
 *  POD 型のみ — MSVC C2712 制約:
 *  __try を持つ関数内では非 trivial デストラクタを持つオブジェクトを
 *  一切（一時変数含む）使用できない。qWarning() も不可。
 *  エラーはフラグで返し、呼び出し元でログ出力する。 */
struct PluginRawInfo {
    bool success      = false;
    bool loadFailed   = false;  ///< LoadLibraryExW が NULL を返した
    bool sehCrashed   = false;  ///< __except で捕捉したハードウェア例外
    char name[256]    = {};
    char vendor[256]  = {};
    char version[256] = {};
    char category[64] = {};
    bool isInstrument = false;
    bool isEffect     = false;
};

/**
 * @brief VST3 DLL をロードしプラグイン情報を SEH 保護下で取得する
 * @param binPathW DLL の絶対パス (wchar_t)
 */
static PluginRawInfo extractPluginInfoSafe(const wchar_t* binPathW)
{
    PluginRawInfo out;

    // OS エラーダイアログ抑制:
    // 依存 DLL が見つからない場合に Windows がモーダルダイアログを出して
    // 無応答になるのを防ぐ。
    UINT prevErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    // LoadLibraryExW を含む全処理を __try で囲み、DllMain クラッシュも捕捉する。
    // !! MSVC C2712 制約: この関数内では非 trivial デストラクタを持つオブジェクトを
    //    一切使用禁止。QString / QDebug 一時変数（qWarning() を含む）も不可。
    //    early return も out のコピーを生成するため除去し else {} 構造に変更。
    HMODULE hLib = nullptr;

    __try {
        // LOAD_WITH_ALTERED_SEARCH_PATH: DLL 依存関係を DLL と同じフォルダから優先解決
        hLib = LoadLibraryExW(binPathW, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!hLib) {
            // 通常のロード失敗 (SEH 例外ではない → __except に入らない)
            // qWarning() 使用不可のためフラグで呼び出し元に通知する
            out.loadFailed = true;
        } else {

        typedef IPluginFactory* (STDMETHODCALLTYPE *GetFactoryProc)();
        GetFactoryProc getFactory = reinterpret_cast<GetFactoryProc>(
            GetProcAddress(hLib, "GetPluginFactory"));

        if (getFactory) {
            IPluginFactory* factory = getFactory();
            if (factory) {
                // ベンダー情報取得
                PFactoryInfo factoryInfo = {};
                if (factory->getFactoryInfo(&factoryInfo) == kResultOk) {
                    strncpy_s(out.vendor, sizeof(out.vendor),
                              factoryInfo.vendor, _TRUNCATE);
                }

                // IPluginFactory2 を直接 queryInterface で取得
                // (FUnknownPtr はデストラクタを持つため __try 内では使用不可)
                IPluginFactory2* factory2 = nullptr;
                TUID iid2;
                memcpy(iid2, IPluginFactory2::iid, sizeof(TUID));
                if (factory->queryInterface(iid2,
                        reinterpret_cast<void**>(&factory2)) == kResultOk
                    && factory2)
                {
                    const int32 classCount = factory2->countClasses();
                    bool sawAudioModuleClass = false;
                    for (int32 i = 0; i < classCount; ++i) {
                        PClassInfo2 ci = {};
                        if (factory2->getClassInfo2(i, &ci) != kResultOk) continue;
                        if (!isAudioModuleCategory(ci.category)) {
                            continue;
                        }

                        sawAudioModuleClass = true;

                        // カテゴリ判定
                        const bool inst =
                            strstr(ci.subCategories, "Instrument") ||
                            strstr(ci.subCategories, "Synth")      ||
                            strstr(ci.subCategories, "Sampler")    ||
                            strstr(ci.category,      "Instrument") ||
                            strstr(ci.category,      "Synth");

                        if (inst) {
                            out.isInstrument = true;
                            strncpy_s(out.category, sizeof(out.category),
                                      "Instrument", _TRUNCATE);
                        }
                        if (strstr(ci.subCategories, "Fx") ||
                            strstr(ci.category,      "Fx")) {
                            out.isEffect = true;
                            if (!out.isInstrument)
                                strncpy_s(out.category, sizeof(out.category),
                                          "Fx", _TRUNCATE);
                        }

                        // プラグイン名 (両端スペースをトリム)
                        const char* nm = ci.name;
                        size_t start   = 0;
                        size_t len     = strnlen_s(nm, sizeof(ci.name));
                        while (start < len && nm[start] == ' ')    ++start;
                        size_t end = len;
                        while (end > start && nm[end - 1] == ' ')  --end;
                        const size_t trimLen = end - start;
                        if (trimLen > 0 && trimLen < sizeof(out.name)) {
                            memcpy(out.name, nm + start, trimLen);
                            out.name[trimLen] = '\0';
                        }

                        // バージョン
                        if (out.version[0] == '\0' && ci.version[0] != '\0')
                            strncpy_s(out.version, sizeof(out.version),
                                      ci.version, _TRUNCATE);
                    }
                    // 非オーディオ class ではなく、audio module だけ見た上で曖昧なら Fx 扱いに寄せる。
                    if (sawAudioModuleClass && !out.isInstrument && !out.isEffect) {
                        out.isEffect = true;
                        strncpy_s(out.category, sizeof(out.category), "Fx", _TRUNCATE);
                    }
                    factory2->release();
                }
                factory->release();
                out.success = true;
            }
        }

        // 正常完了: DLL を安全にアンロード
        // (ここは else { hLib != nullptr } ブロック内なので hLib のチェック不要)
        FreeLibrary(hLib);
        hLib = nullptr;

        } // else (hLib != nullptr) の閉じ括弧
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // qWarning() 使用不可 (QDebug 一時変数 = 非 trivial デストラクタ → C2712)
        // フラグで呼び出し元に通知する
        out.sehCrashed = true;
        // SEH 例外後の FreeLibrary は二次クラッシュを招く危険があるため意図的にスキップ。
        // プロセス終了時に OS が回収する。
    }

    SetErrorMode(prevErrorMode);
    return out;
}

#else // !_MSC_VER (MinGW/GCC)
// ──────────────────────────────────────────
// MinGW/GCC: QLibrary + try/catch によるベストエフォート保護
// ハードウェア例外（アクセス違反）は捕捉できないが、
// C++ 例外およびロード失敗は安全に処理する。
// ──────────────────────────────────────────

struct PluginRawInfo {
    bool success      = false;
    QString name;
    QString vendor;
    QString version;
    QString category;
    bool isInstrument = false;
    bool isEffect     = false;
};

static PluginRawInfo extractPluginInfoSafe(const QString& binPath)
{
    PluginRawInfo out;

    QLibrary lib(binPath);
    if (!lib.load()) {
        qWarning() << "VST3スキャン: ライブラリロード失敗 -" << binPath;
        return out;
    }

    typedef IPluginFactory* (STDMETHODCALLTYPE *GetFactoryProc)();
    GetFactoryProc getFactory = reinterpret_cast<GetFactoryProc>(
        lib.resolve("GetPluginFactory"));

    if (getFactory) {
        try {
            IPluginFactory* factory = getFactory();
            if (factory) {
                // ベンダー情報取得
                PFactoryInfo factoryInfo = {};
                if (factory->getFactoryInfo(&factoryInfo) == kResultOk)
                    out.vendor = QString::fromUtf8(factoryInfo.vendor);

                // IPluginFactory2 によるクラス情報取得
                // FUnknownPtr を使うと factory->release() 後にデストラクタが
                // 解放済みポインタを参照する危険があるため、先にスコープを閉じる
                {
                    FUnknownPtr<IPluginFactory2> factory2(factory);
                    if (factory2) {
                        const int32 classCount = factory2->countClasses();
                        bool sawAudioModuleClass = false;
                        for (int32 i = 0; i < classCount; ++i) {
                            PClassInfo2 ci = {};
                            if (factory2->getClassInfo2(i, &ci) != kResultOk) continue;

                            const QString cat  = QString::fromUtf8(ci.category);
                            if (!isAudioModuleCategory(cat)) {
                                continue;
                            }

                            sawAudioModuleClass = true;
                            const QString sub  = QString::fromUtf8(ci.subCategories);
                            const QString nm   = QString::fromUtf8(ci.name).trimmed();
                            const QString ver  = QString::fromUtf8(ci.version);

                            const bool inst =
                                cat.contains("Instrument", Qt::CaseInsensitive) ||
                                cat.contains("Synth",      Qt::CaseInsensitive) ||
                                sub.contains("Instrument", Qt::CaseInsensitive) ||
                                sub.contains("Synth",      Qt::CaseInsensitive) ||
                                sub.contains("Sampler",    Qt::CaseInsensitive);

                            if (inst) {
                                out.isInstrument = true;
                                out.category     = "Instrument";
                            }
                            if (cat.contains("Fx", Qt::CaseInsensitive) ||
                                sub.contains("Fx", Qt::CaseInsensitive)) {
                                out.isEffect = true;
                                if (out.category.isEmpty()) out.category = "Fx";
                            }

                            if (!nm.isEmpty())          out.name    = nm;
                            if (out.version.isEmpty())  out.version = ver;
                        }
                        if (sawAudioModuleClass && !out.isInstrument && !out.isEffect) {
                            out.isEffect = true;
                            if (out.category.isEmpty()) {
                                out.category = "Fx";
                            }
                        }
                        // factory2 をここで確実に解放してから factory->release() へ
                    } // FUnknownPtr<IPluginFactory2> のデストラクタ実行
                }
                factory->release();
                out.success = true;
            }
        } catch (...) {
            qWarning() << "VST3スキャン: プラグイン処理中に C++ 例外が発生しました。スキップします。"
                       << binPath;
        }
    }

    lib.unload();
    return out;
}

#endif // _MSC_VER
#endif // Q_OS_WIN

#ifdef Q_OS_WIN
bool populateWindowsPluginInfoFromBinaryPath(const QString& binPath, VST3PluginInfo& info)
{
#ifdef _MSC_VER
    const std::wstring binPathW = binPath.toStdWString();
    const PluginRawInfo raw = extractPluginInfoSafe(binPathW.c_str());

    if (raw.loadFailed) {
        qWarning() << "VST3スキャン: DLL ロード失敗 -" << binPath;
        return false;
    }
    if (raw.sehCrashed) {
        qWarning() << "VST3スキャン: プラグイン DLL がクラッシュしました。スキップします -" << binPath;
        return false;
    }
    if (!raw.success) {
        return false;
    }

    if (raw.name[0] != '\0')     info.name     = QString::fromUtf8(raw.name);
    if (raw.vendor[0] != '\0')   info.vendor   = QString::fromUtf8(raw.vendor);
    if (raw.version[0] != '\0')  info.version  = QString::fromUtf8(raw.version);
    if (raw.category[0] != '\0') info.category = QString::fromUtf8(raw.category);
    info.isInstrument = raw.isInstrument;
    info.isEffect     = raw.isEffect;
#else
    const PluginRawInfo raw = extractPluginInfoSafe(binPath);
    if (!raw.success) {
        return false;
    }

    if (!raw.name.isEmpty())     info.name     = raw.name;
    if (!raw.vendor.isEmpty())   info.vendor   = raw.vendor;
    if (!raw.version.isEmpty())  info.version  = raw.version;
    if (!raw.category.isEmpty()) info.category = raw.category;
    info.isInstrument = raw.isInstrument;
    info.isEffect     = raw.isEffect;
#endif

    qDebug() << "VST3スキャン:" << info.name
             << "Inst:" << info.isInstrument
             << "FX:"   << info.isEffect
             << "Binary:" << binPath;
    return true;
}
#endif

VST3Scanner::VST3Scanner(QObject* parent)
    : QObject(parent)
{
    const QStringList storedPaths = loadStoredScanPaths();
    m_scanPaths = storedPaths.isEmpty() ? defaultScanPaths() : storedPaths;
}

QStringList VST3Scanner::defaultScanPaths()
{
    QStringList paths;
#ifdef Q_OS_WIN
    const QString localAppData = environmentPath("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        appendUniquePath(paths, QDir(localAppData).filePath(QStringLiteral("Programs/Common/VST3")));
    }

    const QString commonProgramFiles = environmentPath("COMMONPROGRAMFILES");
    if (!commonProgramFiles.isEmpty()) {
        appendUniquePath(paths, QDir(commonProgramFiles).filePath(QStringLiteral("VST3")));
    }
    const QString commonProgramFilesX86 = environmentPath("COMMONPROGRAMFILES(X86)");
    if (!commonProgramFilesX86.isEmpty()) {
        appendUniquePath(paths, QDir(commonProgramFilesX86).filePath(QStringLiteral("VST3")));
    }

    appendUniquePath(paths, QStringLiteral("C:/Program Files/Common Files/VST3"));
    appendUniquePath(paths, QStringLiteral("C:/Program Files (x86)/Common Files/VST3"));
    appendUniquePath(paths, applicationSpecificVst3Path());
#elif defined(Q_OS_MAC)
    appendUniquePath(paths, QDir::homePath() + QStringLiteral("/Library/Audio/Plug-Ins/VST3"));
    appendUniquePath(paths, QStringLiteral("/Library/Audio/Plug-Ins/VST3"));
    appendUniquePath(paths, QStringLiteral("/Network/Library/Audio/Plug-Ins/VST3"));
    appendUniquePath(paths, applicationSpecificVst3Path());
#elif defined(Q_OS_LINUX)
    paths << "/usr/lib/vst3";
    paths << "/usr/local/lib/vst3";
    paths << QDir::homePath() + "/.vst3";
#endif
    return normalizeScanPaths(paths);
}

void VST3Scanner::addScanPath(const QString& path)
{
    const QString cleanedPath = normalizeScanPath(path);
    if (cleanedPath.isEmpty()) {
        return;
    }
    if (!m_scanPaths.contains(cleanedPath, Qt::CaseInsensitive)) {
        m_scanPaths.append(cleanedPath);
        saveStoredScanPaths(m_scanPaths);
    }
}

QStringList VST3Scanner::scanPaths() const
{
    return m_scanPaths;
}

void VST3Scanner::setScanPaths(const QStringList& paths)
{
    m_scanPaths = normalizeScanPaths(paths);
    saveStoredScanPaths(m_scanPaths);
}

QVector<VST3PluginInfo> VST3Scanner::scan(bool instrumentsOnly)
{
    qDebug() << "VST3スキャン開始...";

    // ネストしたQtConcurrentはスレッドプール枚渇が原因でクラッシュするため、
    // 呼び出し元のQtConcurrent::run一段だけに統一しシリアルスキャンする。
    // スキャン自体はI/Oバウンドなので、並列化による常用な速度差はない。
    QVector<VST3PluginInfo> allPlugins;
    QSet<QString> seenPluginPaths;
    auto appendPluginIfNeeded = [&](const VST3PluginInfo& plugin) {
        if (plugin.name.isEmpty()) {
            return;
        }

        const QString pluginKey = pluginIdentityPath(plugin.path);
        if (!seenPluginPaths.contains(pluginKey)) {
            seenPluginPaths.insert(pluginKey);
            allPlugins.append(plugin);
        }
    };

    for (const QString& path : m_scanPaths) {
        const QString normalizedPath = normalizeScanPath(path);
        if (normalizedPath.isEmpty()) {
            continue;
        }

        const QFileInfo scanPathInfo(normalizedPath);
        if (!scanPathInfo.exists()) {
            continue;
        }

        // `.vst3` 自体を直接 scan path に入れた場合も、その場で解析して取りこぼさない。
        if (scanPathInfo.fileName().endsWith(QStringLiteral(".vst3"), Qt::CaseInsensitive)) {
            appendPluginIfNeeded(parseVST3Bundle(scanPathInfo.absoluteFilePath()));
            continue;
        }

        if (scanPathInfo.isDir()) {
            const QVector<VST3PluginInfo> scannedPlugins =
                scanDirectory(scanPathInfo.absoluteFilePath());
            for (const VST3PluginInfo& plugin : scannedPlugins) {
                appendPluginIfNeeded(plugin);
            }
        }
    }

    qDebug() << "VST3スキャン完了。" << allPlugins.size() << "件検出。";

    if (instrumentsOnly) {
        QVector<VST3PluginInfo> instruments;
        for (const auto& plugin : allPlugins) {
            // `Fx|Instrument` でも instrument としてロードできるものは一覧に残す。
            if (plugin.isInstrument) {
                instruments.append(plugin);
            }
        }
        emit scanComplete(instruments.size());
        return instruments;
    }

    emit scanComplete(allPlugins.size());
    return allPlugins;
}

QVector<VST3PluginInfo> VST3Scanner::scanDirectory(const QString& dir)
{
    QVector<VST3PluginInfo> plugins;
    
    // Find all .vst3 files or folders recursively
    QDirIterator it(dir, QStringList() << "*.vst3", QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    
    while (it.hasNext()) {
        QString path = it.next();
        QFileInfo info(path);
        
        // Optimize: verify logic to avoid redundant scans inside bundles
        // Mac VST3 is a bundle (folder), Windows VST3 is often a file or bundle
        // We need to skip internal contents if we already found the bundle root
        
        // If we find a .vst3 inside another .vst3 bundle, that's weird but possible in some structures
        // Standard VST3 bundle structure: Name.vst3/Contents/...
        
        // Check if path contains another .vst3 directory above it
        QString parentPath = info.path();
        if (parentPath.contains(".vst3", Qt::CaseInsensitive)) {
            // Already inside a VST3 bundle, likely irrelevant binary or resource
            continue;
        }
        
        VST3PluginInfo plugin = parseVST3Bundle(path);
        if (!plugin.name.isEmpty()) {
            plugins.append(plugin);
        }
    }
    return plugins;
}

VST3PluginInfo VST3Scanner::parseVST3Bundle(const QString& path)
{
    VST3PluginInfo info;
    info.path = path;

    QFileInfo fileInfo(path);
    info.name = fileInfo.baseName(); // デフォルト名 (情報取得失敗時のフォールバック)

    // --- バイナリパスの決定 ---
    QStringList windowsBinaryCandidates;
    if (fileInfo.isDir()) {
#ifdef Q_OS_WIN
        // Windows の VST3 bundle は x86_64 以外のアーキディレクトリもあり得るため、
        // 既知の候補を順に試してから動的に列挙する。
        windowsBinaryCandidates = windowsBundleBinaryCandidates(path, info.name);
        if (windowsBinaryCandidates.isEmpty()) {
            qWarning() << "VST3スキャン: バイナリが見つかりません -" << path;
            return info;
        }
#else
        const bool hasModuleInfo = parseModuleInfo(path, info);
#ifdef Q_OS_MAC
        const BundlePlistMetadata plistMetadata = readBundlePlistMetadata(path);
        populateVersionFromBundlePlist(plistMetadata, info);

        // 危険なバイナリロードは最後の手段にして、まずは静的 metadata を使い切る。
        const bool needsHostingFallback =
            info.vendor.isEmpty() || info.version.isEmpty() ||
            (!info.isInstrument && !info.isEffect) || !hasModuleInfo;
        if (needsHostingFallback) {
            Darwin::populateInfoFromVST3ProbeProcess(path, info);
        }
        populateVendorFromBundleIdentifier(plistMetadata, info);

        if (!info.isInstrument && !info.isEffect) {
            info.isEffect = true;
            if (info.category.isEmpty()) {
                info.category = QStringLiteral("Fx");
            }
        }
#endif
        return info;
#endif
    }

#ifdef Q_OS_WIN
    if (windowsBinaryCandidates.isEmpty()) {
        windowsBinaryCandidates << path;
    }

    bool loaded = false;
    for (const QString& candidate : windowsBinaryCandidates) {
        VST3PluginInfo candidateInfo = info;
        if (populateWindowsPluginInfoFromBinaryPath(candidate, candidateInfo)) {
            info = candidateInfo;
            loaded = true;
            break;
        }
    }

    if (!loaded) {
        qWarning() << "VST3スキャン: 読み込み可能な Windows バイナリが見つかりません -" << path;
        return info;
    }
#else
    Q_UNUSED(windowsBinaryCandidates)
#endif // Q_OS_WIN

    return info;
}

bool VST3Scanner::parseModuleInfo(const QString& bundlePath, VST3PluginInfo& info)
{
    QStringList candidates;
    candidates << (bundlePath + QStringLiteral("/Contents/Resources/moduleinfo.json"))
               << (bundlePath + QStringLiteral("/Contents/moduleinfo.json"))
               << (bundlePath + QStringLiteral("/moduleinfo.json"));

    QFile file;
    QString resolvedPath;
    for (const QString& candidate : candidates) {
        if (QFile::exists(candidate)) {
            file.setFileName(candidate);
            resolvedPath = candidate;
            break;
        }
    }

    if (resolvedPath.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return false;
    }

    const QJsonObject root = doc.object();
    bool updated = false;

    if (info.vendor.isEmpty()) {
        const QString vendor = jsonStringByKeys(root, {
            QStringLiteral("Vendor"),
            QStringLiteral("vendor")
        });
        if (!vendor.isEmpty()) {
            info.vendor = vendor;
            updated = true;
        }
    }

    if (info.version.isEmpty()) {
        const QString version = jsonStringByKeys(root, {
            QStringLiteral("Version"),
            QStringLiteral("version")
        });
        if (!version.isEmpty()) {
            info.version = version;
            updated = true;
        }
    }

    const QJsonArray classes = root.value(QStringLiteral("Classes")).toArray().isEmpty()
        ? root.value(QStringLiteral("classes")).toArray()
        : root.value(QStringLiteral("Classes")).toArray();
    for (const QJsonValue& value : classes) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject classObject = value.toObject();
        const QString category = jsonStringByKeys(classObject, {
            QStringLiteral("Category"),
            QStringLiteral("category")
        });
        const QString subCategories = jsonStringList(classObject.value(QStringLiteral("Sub Categories")));
        const QString altSubCategories = subCategories.isEmpty()
            ? jsonStringList(classObject.value(QStringLiteral("SubCategories")))
            : subCategories;
        const QString effectiveSubCategories = altSubCategories.isEmpty()
            ? jsonStringList(classObject.value(QStringLiteral("subCategories")))
            : altSubCategories;

        const QString combined = (category + QStringLiteral(" ") + effectiveSubCategories).trimmed();
        if (!combined.contains(QStringLiteral("Audio"), Qt::CaseInsensitive) &&
            !combined.contains(QStringLiteral("Instrument"), Qt::CaseInsensitive) &&
            !combined.contains(QStringLiteral("Fx"), Qt::CaseInsensitive) &&
            !combined.contains(QStringLiteral("Effect"), Qt::CaseInsensitive)) {
            continue;
        }

        const QString className = jsonStringByKeys(classObject, {
            QStringLiteral("Name"),
            QStringLiteral("name")
        });
        if (!className.isEmpty()) {
            info.name = className;
            updated = true;
        }

        if (info.vendor.isEmpty()) {
            const QString vendor = jsonStringByKeys(classObject, {
                QStringLiteral("Vendor"),
                QStringLiteral("vendor")
            });
            if (!vendor.isEmpty()) {
                info.vendor = vendor;
                updated = true;
            }
        }

        if (info.version.isEmpty()) {
            const QString version = jsonStringByKeys(classObject, {
                QStringLiteral("Version"),
                QStringLiteral("version")
            });
            if (!version.isEmpty()) {
                info.version = version;
                updated = true;
            }
        }

        classifyPlugin(category, effectiveSubCategories, info);
        updated = true;

        if (info.isInstrument) {
            break;
        }
    }

    return updated;
}
