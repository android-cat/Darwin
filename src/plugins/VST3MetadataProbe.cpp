#include "VST3MetadataProbe.h"

#include "VST3Scanner.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include <cstdlib>
#include <cstdio>
#include <exception>

#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

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

    // probe 側でも種別不明のまま返さないよう、最後は Fx に寄せる。
    if (!info.isInstrument && !info.isEffect) {
        info.isEffect = true;
        if (info.category.isEmpty()) {
            info.category = categoryText.isEmpty() ? QStringLiteral("Fx") : categoryText;
        }
    }
}

bool populateInfoFromHostingModule(const QString& modulePath, VST3PluginInfo& info)
{
    try {
        std::string errorDescription;
        auto module = VST3::Hosting::Module::create(modulePath.toStdString(), errorDescription);
        if (!module) {
            qWarning() << "VST3 probe: Module::create に失敗 -" << modulePath
                       << "-" << QString::fromStdString(errorDescription);
            return false;
        }

        auto& factory = module->getFactory();
        const QString factoryVendor = QString::fromStdString(factory.info().vendor()).trimmed();
        if (info.vendor.isEmpty() && !factoryVendor.isEmpty()) {
            info.vendor = factoryVendor;
        }

        bool updated = !factoryVendor.isEmpty();
        const QString fallbackName = QFileInfo(modulePath).baseName();
        for (const auto& classInfo : factory.classInfos()) {
            if (classInfo.category() != kVstAudioEffectClass) {
                continue;
            }

            const QString className = QString::fromStdString(classInfo.name()).trimmed();
            if ((info.name.isEmpty() || info.name == fallbackName) && !className.isEmpty()) {
                info.name = className;
                updated = true;
            }

            if (info.vendor.isEmpty()) {
                const QString classVendor = QString::fromStdString(classInfo.vendor()).trimmed();
                if (!classVendor.isEmpty()) {
                    info.vendor = classVendor;
                    updated = true;
                }
            }

            if (info.version.isEmpty()) {
                const QString classVersion = QString::fromStdString(classInfo.version()).trimmed();
                if (!classVersion.isEmpty()) {
                    info.version = classVersion;
                    updated = true;
                }
            }

            classifyPlugin(QString::fromStdString(classInfo.category()),
                           QString::fromStdString(classInfo.subCategoriesString()),
                           info);
            updated = true;
        }

        if (!updated && info.name.isEmpty()) {
            info.name = QFileInfo(modulePath).baseName();
        }

        // ここで return すると module のアンロードが走って一部プラグインが落ちるため、
        // 呼び出し側では JSON 出力後に std::_Exit(0) で helper ごと終了する前提にする。
        QJsonObject root;
        root.insert(QStringLiteral("success"), updated);
        root.insert(QStringLiteral("name"), info.name);
        root.insert(QStringLiteral("vendor"), info.vendor);
        root.insert(QStringLiteral("version"), info.version);
        root.insert(QStringLiteral("category"), info.category);
        root.insert(QStringLiteral("isInstrument"), info.isInstrument);
        root.insert(QStringLiteral("isEffect"), info.isEffect);

        const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
        if (!payload.isEmpty()) {
            std::fwrite(payload.constData(), 1, static_cast<size_t>(payload.size()), stdout);
            std::fputc('\n', stdout);
            std::fflush(stdout);
        }

        std::_Exit(0);
    } catch (const std::exception& ex) {
        qWarning() << "VST3 probe: 例外 -" << modulePath << "-" << ex.what();
    } catch (...) {
        qWarning() << "VST3 probe: 未知の例外 -" << modulePath;
    }

    return false;
}

bool applyProbePayload(const QByteArray& payload, VST3PluginInfo& info)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    const QJsonObject root = document.object();
    bool updated = false;

    const QString name = root.value(QStringLiteral("name")).toString().trimmed();
    const QString fallbackName = QFileInfo(info.path).baseName();
    if ((info.name.isEmpty() || info.name == fallbackName) && !name.isEmpty()) {
        info.name = name;
        updated = true;
    }

    const QString vendor = root.value(QStringLiteral("vendor")).toString().trimmed();
    if (info.vendor.isEmpty() && !vendor.isEmpty()) {
        info.vendor = vendor;
        updated = true;
    }

    const QString version = root.value(QStringLiteral("version")).toString().trimmed();
    if (info.version.isEmpty() && !version.isEmpty()) {
        info.version = version;
        updated = true;
    }

    const QString category = root.value(QStringLiteral("category")).toString().trimmed();
    if (info.category.isEmpty() && !category.isEmpty()) {
        info.category = category;
        updated = true;
    }

    if (!info.isInstrument && root.value(QStringLiteral("isInstrument")).toBool()) {
        info.isInstrument = true;
        updated = true;
    }
    if (!info.isEffect && root.value(QStringLiteral("isEffect")).toBool()) {
        info.isEffect = true;
        updated = true;
    }

    return updated;
}

} // namespace

namespace Darwin {

bool tryParseVST3MetadataProbeArgs(int argc, char* argv[], QString& modulePath)
{
    if (argc < 3) {
        return false;
    }

    const QString command = QString::fromLocal8Bit(argv[1]);
    if (command != QStringLiteral("--vst3-metadata-probe")) {
        return false;
    }

    modulePath = QString::fromLocal8Bit(argv[2]);
    return !modulePath.trimmed().isEmpty();
}

int runVST3MetadataProbe(const QString& modulePath)
{
#ifdef Q_OS_MAC
    VST3PluginInfo info;
    info.path = modulePath;
    info.name = QFileInfo(modulePath).baseName();
    if (populateInfoFromHostingModule(modulePath, info)) {
        // populateInfoFromHostingModule は正常系で std::_Exit(0) する。
        return 0;
    }
#else
    Q_UNUSED(modulePath)
#endif
    return 1;
}

bool populateInfoFromVST3ProbeProcess(const QString& modulePath,
                                      VST3PluginInfo& info,
                                      int timeoutMs)
{
#ifdef Q_OS_MAC
    QProcess process;
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments({QStringLiteral("--vst3-metadata-probe"), modulePath});
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();

    if (!process.waitForStarted(2000)) {
        qWarning() << "VST3 probe: helper 起動に失敗 -" << modulePath
                   << "-" << process.errorString();
        return false;
    }

    if (!process.waitForFinished(timeoutMs)) {
        qWarning() << "VST3 probe: helper がタイムアウトしたため終了します -" << modulePath;
        process.kill();
        process.waitForFinished(2000);
        return false;
    }

    const QByteArray stdoutData = process.readAllStandardOutput().trimmed();
    const QByteArray stderrData = process.readAllStandardError().trimmed();
    if (!stderrData.isEmpty()) {
        qWarning() << "VST3 probe stderr:" << stderrData;
    }

    const bool updated = applyProbePayload(stdoutData, info);
    if (!updated && process.exitStatus() == QProcess::CrashExit) {
        qWarning() << "VST3 probe: helper がクラッシュしました -" << modulePath;
    }
    return updated;
#else
    Q_UNUSED(modulePath)
    Q_UNUSED(info)
    Q_UNUSED(timeoutMs)
    return false;
#endif
}

} // namespace Darwin
