#include "VST3Scanner.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QStandardPaths>
#include <QDebug>
#include <QFileInfo>
#include <QLibrary>

// VST3 SDK Interfaces
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
#include "pluginterfaces/vst/vsttypes.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

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
                    for (int32 i = 0; i < classCount; ++i) {
                        PClassInfo2 ci = {};
                        if (factory2->getClassInfo2(i, &ci) != kResultOk) continue;

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
                        // どちらでもない場合はエフェクト扱い (フォールバック)
                        if (!out.isInstrument && !out.isEffect)
                            out.isEffect = true;

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
                        for (int32 i = 0; i < classCount; ++i) {
                            PClassInfo2 ci = {};
                            if (factory2->getClassInfo2(i, &ci) != kResultOk) continue;

                            const QString cat  = QString::fromUtf8(ci.category);
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
                            if (!out.isInstrument && !out.isEffect)
                                out.isEffect = true; // フォールバック

                            if (!nm.isEmpty())          out.name    = nm;
                            if (out.version.isEmpty())  out.version = ver;
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

VST3Scanner::VST3Scanner(QObject* parent)
    : QObject(parent)
{
    m_scanPaths = defaultScanPaths();
}

QStringList VST3Scanner::defaultScanPaths()
{
    QStringList paths;
#ifdef Q_OS_WIN
    paths << "C:/Program Files/Common Files/VST3";
    paths << "C:/Program Files (x86)/Common Files/VST3";
    QString userVst3 = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/VST3";
    if (!userVst3.isEmpty()) paths << userVst3;
#elif defined(Q_OS_MAC)
    paths << "/Library/Audio/Plug-Ins/VST3";
    paths << QDir::homePath() + "/Library/Audio/Plug-Ins/VST3";
#elif defined(Q_OS_LINUX)
    paths << "/usr/lib/vst3";
    paths << "/usr/local/lib/vst3";
    paths << QDir::homePath() + "/.vst3";
#endif
    return paths;
}

void VST3Scanner::addScanPath(const QString& path)
{
    if (!m_scanPaths.contains(path)) m_scanPaths.append(path);
}

QStringList VST3Scanner::scanPaths() const
{
    return m_scanPaths;
}

void VST3Scanner::setScanPaths(const QStringList& paths)
{
    m_scanPaths = paths;
}

QVector<VST3PluginInfo> VST3Scanner::scan(bool instrumentsOnly)
{
    qDebug() << "VST3スキャン開始...";

    // ネストしたQtConcurrentはスレッドプール枚渇が原因でクラッシュするため、
    // 呼び出し元のQtConcurrent::run一段だけに統一しシリアルスキャンする。
    // スキャン自体はI/Oバウンドなので、並列化による常用な速度差はない。
    QVector<VST3PluginInfo> allPlugins;
    for (const QString& path : m_scanPaths) {
        if (QDir(path).exists()) {
            allPlugins.append(scanDirectory(path));
        }
    }

    qDebug() << "VST3スキャン完了。" << allPlugins.size() << "件検出。";

    if (instrumentsOnly) {
        QVector<VST3PluginInfo> instruments;
        for (const auto& plugin : allPlugins) {
            if (plugin.isInstrument && !plugin.isEffect) {
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
    QString binPath = path;
    if (fileInfo.isDir()) {
        // バンドル形式: Contents/x86_64-win/<name>.vst3 にDLLがある
#ifdef Q_OS_WIN
        const QString subPath = "/Contents/x86_64-win/" + info.name + ".vst3";
        binPath = path + subPath;
        if (!QFile::exists(binPath)) {
            QDir archDir(path + "/Contents/x86_64-win");
            const QStringList entries = archDir.entryList(
                QStringList() << "*.vst3", QDir::Files);
            if (!entries.isEmpty()) {
                binPath = archDir.absoluteFilePath(entries.first());
            } else {
                qWarning() << "VST3スキャン: バイナリが見つかりません -" << path;
                return info;
            }
        }
#else
        return info; // Mac/Linux は未実装
#endif
    }

#ifdef Q_OS_WIN
    // --- SEH / QLibrary 保護下で DLL 情報を取得 ---
#ifdef _MSC_VER
    const std::wstring binPathW = binPath.toStdWString();
    const PluginRawInfo raw = extractPluginInfoSafe(binPathW.c_str());

    // extractPluginInfoSafe 内では qWarning() 使用不可のため、ここでログ出力する
    if (raw.loadFailed) {
        qWarning() << "VST3スキャン: DLL ロード失敗 -" << binPath;
        return info;
    }
    if (raw.sehCrashed) {
        qWarning() << "VST3スキャン: プラグイン DLL がクラッシュしました。スキップします -" << binPath;
        return info;
    }
    if (!raw.success) {
        return info;
    }

    // 取得結果を VST3PluginInfo に変換 (MSVC: char バッファ → QString)
    if (raw.name[0] != '\0')     info.name     = QString::fromUtf8(raw.name);
    if (raw.vendor[0] != '\0')   info.vendor   = QString::fromUtf8(raw.vendor);
    if (raw.version[0] != '\0')  info.version  = QString::fromUtf8(raw.version);
    if (raw.category[0] != '\0') info.category = QString::fromUtf8(raw.category);
    info.isInstrument = raw.isInstrument;
    info.isEffect     = raw.isEffect;
#else
    // MinGW: PluginRawInfo は QString メンバーを持つ
    const PluginRawInfo raw = extractPluginInfoSafe(binPath);

    if (!raw.success) {
        return info;
    }

    if (!raw.name.isEmpty())    info.name     = raw.name;
    if (!raw.vendor.isEmpty())  info.vendor   = raw.vendor;
    if (!raw.version.isEmpty()) info.version  = raw.version;
    if (!raw.category.isEmpty())info.category = raw.category;
    info.isInstrument = raw.isInstrument;
    info.isEffect     = raw.isEffect;
#endif // _MSC_VER

    qDebug() << "VST3スキャン:" << info.name
             << "Inst:" << info.isInstrument
             << "FX:"   << info.isEffect;
#else
    Q_UNUSED(binPath)
#endif // Q_OS_WIN

    return info;
}

// Stub for parseModuleInfo if declaration exists in header but not used
bool VST3Scanner::parseModuleInfo(const QString&, VST3PluginInfo&) { return false; }
