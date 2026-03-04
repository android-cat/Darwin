#include "VST3Scanner.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QStandardPaths>
#include <QDebug>
#include <QLibrary>
#include <QFileInfo>

// VST3 SDK Interfaces
#ifdef Q_OS_WIN
#define SMTG_OS_WINDOWS 1
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
    info.isInstrument = false;
    info.isEffect = false;
    
    QFileInfo fileInfo(path);
    info.name = fileInfo.baseName(); // Default name
    
    // Determine the actual binary path
    QString binPath = path;
    if (fileInfo.isDir()) {
        // It's a bundle, find the binary
#ifdef Q_OS_WIN
        QString subPath = "/Contents/x86_64-win/" + info.name + ".vst3";
        binPath = path + subPath;
        if (!QFile::exists(binPath)) {
             // Try to find any .vst3 file in the architecture folder
             QDir archDir(path + "/Contents/x86_64-win");
             QStringList entries = archDir.entryList(QStringList() << "*.vst3", QDir::Files);
             if (!entries.isEmpty()) {
                 binPath = archDir.absoluteFilePath(entries.first());
             } else {
                 return info; // Failed to find binary
             }
        }
#else
        // Mac/Linux logic
        return info; 
#endif
    }
    
    // Load Library
    QLibrary lib(binPath);
    if (!lib.load()) {
        // Handle failure?
        return info;
    }
    
    typedef IPluginFactory* (STDMETHODCALLTYPE *GetFactoryProc)();
    GetFactoryProc getFactory = (GetFactoryProc)lib.resolve("GetPluginFactory");
    
    if (getFactory) {
        IPluginFactory* factory = getFactory();
        if (factory) {
            // Get Factory Info
            PFactoryInfo factoryInfo;
            if (factory->getFactoryInfo(&factoryInfo) == kResultOk) {
                info.vendor = QString::fromUtf8(factoryInfo.vendor);
            }
            
            // クラス情報の確認
            FUnknownPtr<IPluginFactory2> factory2(factory);
            if (factory2) {
                int32 classCount = factory2->countClasses();
                for (int32 i = 0; i < classCount; ++i) {
                    PClassInfo2 classInfo;
                    if (factory2->getClassInfo2(i, &classInfo) == kResultOk) {
                        QString category = QString::fromUtf8(classInfo.category);
                        QString name = QString::fromUtf8(classInfo.name);
                        QString subCategories = QString::fromUtf8(classInfo.subCategories);
                        QString version = QString::fromUtf8(classInfo.version);

                        if (!version.isEmpty() && info.version.isEmpty()) {
                            info.version = version;
                        }

                        // カテゴリ判定
                        bool isInst = false;
                        if (category.contains("Instrument", Qt::CaseInsensitive) || subCategories.contains("Instrument", Qt::CaseInsensitive)) {
                            isInst = true;
                            info.category = "Instrument";
                        } else if (category.contains("Synth", Qt::CaseInsensitive) || subCategories.contains("Synth", Qt::CaseInsensitive)) {
                             isInst = true;
                             info.category = "Instrument";
                        } else if (category.contains("Sampler", Qt::CaseInsensitive) || subCategories.contains("Sampler", Qt::CaseInsensitive)) {
                             isInst = true;
                             info.category = "Instrument";
                        }

                        if (isInst) {
                            info.isInstrument = true;
                        }

                        // エフェクト判定
                        if (category.contains("Fx", Qt::CaseInsensitive) || subCategories.contains("Fx", Qt::CaseInsensitive)) {
                            info.isEffect = true;
                            if (info.category.isEmpty()) info.category = "Fx";
                        }

                        // インストルメント・エフェクトどちらでもない場合のフォールバック
                        if (!info.isInstrument && !info.isEffect) {
                            info.isEffect = true;
                        }

                        if (info.isInstrument || info.isEffect) {
                            const QString trimmedName = name.trimmed();
                            if (!trimmedName.isEmpty()) {
                                info.name = trimmedName;
                            }
                            qDebug() << "VSTScanner:" << info.name << "Inst:" << info.isInstrument << "FX:" << info.isEffect;
                        }
                    }
                }
            }
            // VST3のCOMライクな参照カウントを解放。
            // これを忝れると、その後のlib.unload()時に
            // FUnknownPtrのデストラクタが解放済メモリを参照しクラッシュする。
            factory->release();
        }
    }

    lib.unload();
    return info;
}

// Stub for parseModuleInfo if declaration exists in header but not used
bool VST3Scanner::parseModuleInfo(const QString&, VST3PluginInfo&) { return false; }
