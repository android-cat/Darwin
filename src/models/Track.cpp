#include "Track.h"
#include "Clip.h"
#include "Note.h"
#include "VST3PluginInstance.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QElapsedTimer>

int Track::s_nextId = 1;

Track::Track(const QString& name, QObject* parent)
    : QObject(parent)
    , m_id(s_nextId++)
    , m_name(name)
    , m_instrumentName()
    , m_visible(true)
    , m_muted(false)
    , m_solo(false)
    , m_volume(1.0)  // デフォルト 0dB
    , m_pan(0.0)
    , m_timingOffsetMs(0.0)
    , m_color(defaultColors()[(m_id - 1) % defaultColors().size()])
    , m_isFolder(false)
    , m_parentFolderId(-1)
    , m_folderExpanded(true)
    , m_pluginInstance(nullptr)
{
}

const QList<QColor>& Track::defaultColors()
{
    // DAW向けの鮮やかなカラーパレット
    static const QList<QColor> colors = {
        QColor("#FF3366"), // ローズ
        QColor("#3b82f6"), // ブルー
        QColor("#10b981"), // エメラルド
        QColor("#f59e0b"), // アンバー
        QColor("#8b5cf6"), // バイオレット
        QColor("#ec4899"), // ピンク
        QColor("#06b6d4"), // シアン
        QColor("#ef4444"), // レッド
        QColor("#84cc16"), // ライム
        QColor("#f97316"), // オレンジ
    };
    return colors;
}

Track::~Track()
{
    clearFxPlugins();
    unloadPlugin();
    clearClips();
}

void Track::setName(const QString& name)
{
    if (m_name != name) {
        m_name = name;
        emit propertyChanged();
    }
}

void Track::setInstrumentName(const QString& instrumentName)
{
    if (m_instrumentName != instrumentName) {
        m_instrumentName = instrumentName;
        emit propertyChanged();
    }
}

void Track::setVisible(bool visible)
{
    if (m_visible != visible) {
        m_visible = visible;
        emit propertyChanged();
    }
}

void Track::setMuted(bool muted)
{
    if (m_muted != muted) {
        m_muted = muted;
        emit propertyChanged();
    }
}

void Track::setSolo(bool solo)
{
    if (m_solo != solo) {
        m_solo = solo;
        emit propertyChanged();
    }
}

void Track::setVolume(double volume)
{
    volume = qBound(0.0, volume, 1.0);
    if (!qFuzzyCompare(m_volume, volume)) {
        m_volume = volume;
        emit propertyChanged();
    }
}

void Track::setPan(double pan)
{
    pan = qBound(-1.0, pan, 1.0);
    if (!qFuzzyCompare(m_pan, pan)) {
        m_pan = pan;
        emit propertyChanged();
    }
}

void Track::setTimingOffsetMs(double ms)
{
    ms = qBound(-100.0, ms, 100.0);
    if (!qFuzzyCompare(m_timingOffsetMs, ms)) {
        m_timingOffsetMs = ms;
        emit propertyChanged();
    }
}

void Track::setColor(const QColor& color)
{
    if (m_color != color) {
        m_color = color;
        emit propertyChanged();
    }
}

void Track::setIsFolder(bool isFolder)
{
    if (m_isFolder != isFolder) {
        m_isFolder = isFolder;
        emit propertyChanged();
    }
}

void Track::setParentFolderId(int folderId)
{
    if (m_parentFolderId != folderId) {
        m_parentFolderId = folderId;
        emit propertyChanged();
    }
}

void Track::setFolderExpanded(bool expanded)
{
    if (m_folderExpanded != expanded) {
        m_folderExpanded = expanded;
        emit propertyChanged();
    }
}

Clip* Track::addClip(qint64 startTick, qint64 durationTicks)
{
    Clip* clip = new Clip(startTick, durationTicks, this);
    m_clips.append(clip);
    
    emit clipAdded(clip);
    emit propertyChanged();
    
    return clip;
}

void Track::removeClip(Clip* clip)
{
    if (m_clips.removeOne(clip)) {
        emit clipRemoved(clip);
        emit propertyChanged();
        clip->deleteLater();
    }
}

Clip* Track::takeClip(Clip* clip)
{
    if (m_clips.removeOne(clip)) {
        emit clipRemoved(clip);
        emit propertyChanged();
        return clip;
    }
    return nullptr;
}

void Track::insertClip(Clip* clip)
{
    if (!clip) return;
    clip->setParent(this);
    m_clips.append(clip);
    emit clipAdded(clip);
    emit propertyChanged();
}

void Track::clearClips()
{
    for (Clip* clip : m_clips) {
        emit clipRemoved(clip);
        clip->deleteLater();
    }
    m_clips.clear();
    emit propertyChanged();
}

Clip* Track::clipAt(qint64 tick) const
{
    for (Clip* clip : m_clips) {
        if (tick >= clip->startTick() && tick < clip->endTick()) {
            return clip;
        }
    }
    return nullptr;
}

bool Track::loadPlugin(const QString& pluginPath)
{
    // 既存プラグインをアンロード
    unloadPlugin();

    auto* instance = new VST3PluginInstance(this);
    if (!instance->load(pluginPath)) {
        delete instance;
        return false;
    }

    {
        QMutexLocker lock(&m_pluginMutex);
        m_pluginInstance = instance;
    }
    QString pName = instance->pluginName();
    setInstrumentName(pName);
    
    // トラック名が未設定またはデフォルト名（"New Track"）の場合はプラグイン名を適用
    if (m_name.isEmpty() || m_name == "New Track") {
        setName(pName);
    }
    emit propertyChanged();
    return true;
}

void Track::setLoadedPlugin(VST3PluginInstance* instance)
{
    unloadPlugin();
    if (!instance) return;
    instance->setParent(this);
    {
        QMutexLocker lock(&m_pluginMutex);
        m_pluginInstance = instance;
    }
    setInstrumentName(instance->pluginName());
    emit propertyChanged();
}

void Track::unloadPlugin()
{
    if (m_pluginInstance) {
        VST3PluginInstance* old = nullptr;
        {
            QMutexLocker lock(&m_pluginMutex);
            old = m_pluginInstance;
            m_pluginInstance = nullptr;
        }
        // ミューテックス解放後に削除（オーディオスレッドは既にnullptrを見てスキップする）
        delete old;
        emit propertyChanged();
    }
}

bool Track::addFxPlugin(const QString& pluginPath)
{
    auto* instance = new VST3PluginInstance(this);
    if (!instance->load(pluginPath)) {
        delete instance;
        return false;
    }

    m_fxPlugins.append(instance);
    emit propertyChanged();
    return true;
}

void Track::removeFxPlugin(int index)
{
    if (index >= 0 && index < m_fxPlugins.size()) {
        VST3PluginInstance* instance = nullptr;
        {
            QMutexLocker lock(&m_pluginMutex);
            instance = m_fxPlugins.takeAt(index);
        }
        delete instance;
        emit propertyChanged();
    }
}

VST3PluginInstance* Track::takeFxPlugin(int index)
{
    VST3PluginInstance* instance = nullptr;
    if (index >= 0 && index < m_fxPlugins.size()) {
        QMutexLocker lock(&m_pluginMutex);
        instance = m_fxPlugins.takeAt(index);
        emit propertyChanged();
    }
    return instance;
}

void Track::insertFxPlugin(int index, VST3PluginInstance* plugin)
{
    if (!plugin) return;
    
    plugin->setParent(this);
    
    QMutexLocker lock(&m_pluginMutex);
    index = qBound(0, index, m_fxPlugins.size());
    m_fxPlugins.insert(index, plugin);
    emit propertyChanged();
}

void Track::moveFxPlugin(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= m_fxPlugins.size() || 
        toIndex < 0 || toIndex > m_fxPlugins.size() || 
        fromIndex == toIndex) {
        return;
    }
    
    QMutexLocker lock(&m_pluginMutex);
    m_fxPlugins.move(fromIndex, toIndex);
    emit propertyChanged();
}

void Track::clearFxPlugins()
{
    QList<VST3PluginInstance*> toDelete;
    {
        QMutexLocker lock(&m_pluginMutex);
        toDelete = m_fxPlugins;
        m_fxPlugins.clear();
    }
    for (auto* instance : toDelete) {
        delete instance;
    }
    emit propertyChanged();
}

QJsonObject Track::toJson() const
{
    QJsonObject json;
    json["id"] = m_id;
    json["name"] = m_name;
    json["instrumentName"] = m_instrumentName;
    json["visible"] = m_visible;
    json["muted"] = m_muted;
    json["solo"] = m_solo;
    json["volume"] = m_volume;
    json["pan"] = m_pan;
    json["timingOffsetMs"] = m_timingOffsetMs;
    json["color"] = m_color.name();

    // フォルダ関連
    json["isFolder"] = m_isFolder;
    json["parentFolderId"] = m_parentFolderId;
    json["folderExpanded"] = m_folderExpanded;

    // プラグインパス＆状態（再ロード＆復元用）
    if (m_pluginInstance && m_pluginInstance->isLoaded()) {
        json["pluginPath"] = m_pluginInstance->pluginPath();
        // プラグインの内部状態をBase64エンコードして保存
        QByteArray componentState = m_pluginInstance->getComponentState();
        if (!componentState.isEmpty()) {
            json["pluginComponentState"] = QString::fromLatin1(componentState.toBase64());
        }
        QByteArray controllerState = m_pluginInstance->getControllerState();
        if (!controllerState.isEmpty()) {
            json["pluginControllerState"] = QString::fromLatin1(controllerState.toBase64());
        }
    }

    // FXプラグインパス＆状態
    QJsonArray fxArray;
    for (const VST3PluginInstance* fx : m_fxPlugins) {
        if (fx && fx->isLoaded()) {
            QJsonObject fxJson;
            fxJson["path"] = fx->pluginPath();
            // FXプラグインの内部状態を保存
            QByteArray fxCompState = fx->getComponentState();
            if (!fxCompState.isEmpty()) {
                fxJson["componentState"] = QString::fromLatin1(fxCompState.toBase64());
            }
            QByteArray fxCtrlState = fx->getControllerState();
            if (!fxCtrlState.isEmpty()) {
                fxJson["controllerState"] = QString::fromLatin1(fxCtrlState.toBase64());
            }
            fxArray.append(fxJson);
        }
    }
    if (!fxArray.isEmpty()) {
        json["fxPlugins"] = fxArray;
    }

    // クリップ
    QJsonArray clipsArray;
    for (const Clip* clip : m_clips) {
        clipsArray.append(clip->toJson());
    }
    json["clips"] = clipsArray;

    return json;
}

Track* Track::fromJson(const QJsonObject& json, QObject* parent, bool deferPluginRestore)
{
    QElapsedTimer uiYieldClock;
    uiYieldClock.start();
    auto yieldUi = [&uiYieldClock]() {
        if (uiYieldClock.elapsed() >= 8) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 2);
            uiYieldClock.restart();
        }
    };

    QString name = json["name"].toString("New Track");
    auto* track = new Track(name, parent);

    // 保存されたIDを復元し、s_nextIdを衝突しないよう更新
    if (json.contains("id")) {
        int savedId = json["id"].toInt(-1);
        if (savedId > 0) {
            track->m_id = savedId;
            if (savedId >= s_nextId) {
                s_nextId = savedId + 1;
            }
        }
    }

    track->setInstrumentName(json["instrumentName"].toString());
    track->setVisible(json["visible"].toBool(true));
    track->setMuted(json["muted"].toBool(false));
    track->setSolo(json["solo"].toBool(false));
    track->setVolume(json["volume"].toDouble(1.0));
    track->setPan(json["pan"].toDouble(0.0));
    track->setTimingOffsetMs(json["timingOffsetMs"].toDouble(0.0));
    if (json.contains("color")) {
        track->setColor(QColor(json["color"].toString()));
    }

    // フォルダ関連
    track->setIsFolder(json["isFolder"].toBool(false));
    track->setParentFolderId(json["parentFolderId"].toInt(-1));
    track->setFolderExpanded(json["folderExpanded"].toBool(true));

    // インストゥルメントプラグイン再ロード＆状態復元
    if (json.contains("pluginPath")) {
        QString pluginPath = json["pluginPath"].toString();
        if (!pluginPath.isEmpty()) {
            if (deferPluginRestore) {
                track->m_hasDeferredPluginRestore = true;
                track->m_pendingPluginPath = pluginPath;
                if (json.contains("pluginComponentState")) {
                    track->m_pendingPluginComponentState = QByteArray::fromBase64(
                        json["pluginComponentState"].toString().toLatin1());
                }
                if (json.contains("pluginControllerState")) {
                    track->m_pendingPluginControllerState = QByteArray::fromBase64(
                        json["pluginControllerState"].toString().toLatin1());
                }
            } else if (track->loadPlugin(pluginPath)) {
                // プラグインの内部状態を復元
                if (json.contains("pluginComponentState")) {
                    QByteArray compState = QByteArray::fromBase64(
                        json["pluginComponentState"].toString().toLatin1());
                    track->m_pluginInstance->setComponentState(compState);
                }
                if (json.contains("pluginControllerState")) {
                    QByteArray ctrlState = QByteArray::fromBase64(
                        json["pluginControllerState"].toString().toLatin1());
                    track->m_pluginInstance->setControllerState(ctrlState);
                }
            }
        }
    }
    yieldUi();

    // FXプラグイン再ロード＆状態復元
    if (json.contains("fxPlugins")) {
        QJsonArray fxArray = json["fxPlugins"].toArray();
        for (const QJsonValue& val : fxArray) {
            QJsonObject fxJson = val.toObject();
            QString fxPath = fxJson["path"].toString();
            if (!fxPath.isEmpty()) {
                if (deferPluginRestore) {
                    track->m_hasDeferredPluginRestore = true;
                    PendingFxState pendingFx;
                    pendingFx.path = fxPath;
                    if (fxJson.contains("componentState")) {
                        pendingFx.componentState = QByteArray::fromBase64(
                            fxJson["componentState"].toString().toLatin1());
                    }
                    if (fxJson.contains("controllerState")) {
                        pendingFx.controllerState = QByteArray::fromBase64(
                            fxJson["controllerState"].toString().toLatin1());
                    }
                    track->m_pendingFxStates.append(pendingFx);
                } else if (track->addFxPlugin(fxPath)) {
                    VST3PluginInstance* fxInstance = track->m_fxPlugins.last();
                    // FXプラグインの内部状態を復元
                    if (fxJson.contains("componentState")) {
                        QByteArray compState = QByteArray::fromBase64(
                            fxJson["componentState"].toString().toLatin1());
                        fxInstance->setComponentState(compState);
                    }
                    if (fxJson.contains("controllerState")) {
                        QByteArray ctrlState = QByteArray::fromBase64(
                            fxJson["controllerState"].toString().toLatin1());
                        fxInstance->setControllerState(ctrlState);
                    }
                }
            }
            yieldUi();
        }
    }

    // クリップ
    QJsonArray clipsArray = json["clips"].toArray();
    for (const QJsonValue& val : clipsArray) {
        Clip* clip = Clip::fromJson(val.toObject(), track);
        track->m_clips.append(clip);
        yieldUi();
    }

    return track;
}

void Track::restoreDeferredPlugins()
{
    if (!m_hasDeferredPluginRestore) {
        return;
    }

    if (!m_pendingPluginPath.isEmpty() && loadPlugin(m_pendingPluginPath) && m_pluginInstance) {
        if (!m_pendingPluginComponentState.isEmpty()) {
            m_pluginInstance->setComponentState(m_pendingPluginComponentState);
        }
        if (!m_pendingPluginControllerState.isEmpty()) {
            m_pluginInstance->setControllerState(m_pendingPluginControllerState);
        }
    }

    for (const PendingFxState& pendingFx : m_pendingFxStates) {
        if (pendingFx.path.isEmpty()) {
            continue;
        }
        if (addFxPlugin(pendingFx.path)) {
            VST3PluginInstance* fxInstance = m_fxPlugins.last();
            if (!pendingFx.componentState.isEmpty()) {
                fxInstance->setComponentState(pendingFx.componentState);
            }
            if (!pendingFx.controllerState.isEmpty()) {
                fxInstance->setControllerState(pendingFx.controllerState);
            }
        }
    }

    m_pendingPluginPath.clear();
    m_pendingPluginComponentState.clear();
    m_pendingPluginControllerState.clear();
    m_pendingFxStates.clear();
    m_hasDeferredPluginRestore = false;
}
