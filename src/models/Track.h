#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QColor>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>

class Clip;
class VST3PluginInstance;

/**
 * @brief トラックを表すクラス
 */
class Track : public QObject
{
    Q_OBJECT

    struct PendingFxState {
        QString path;
        QByteArray componentState;
        QByteArray controllerState;
    };

public:
    explicit Track(const QString& name = "New Track", QObject* parent = nullptr);
    ~Track() override;

    // Getters
    int id() const { return m_id; }
    QString name() const { return m_name; }
    QString instrumentName() const { return m_instrumentName; }
    bool isVisible() const { return m_visible; }
    bool isMuted() const { return m_muted; }
    bool isSolo() const { return m_solo; }
    double volume() const { return m_volume; }
    double pan() const { return m_pan; }
    double timingOffsetMs() const { return m_timingOffsetMs; }
    QColor color() const { return m_color; }
    const QList<Clip*>& clips() const { return m_clips; }
    VST3PluginInstance* pluginInstance() const { return m_pluginInstance; }
    bool hasPlugin() const { return m_pluginInstance != nullptr; }

    // フォルダ関連
    bool isFolder() const { return m_isFolder; }
    int parentFolderId() const { return m_parentFolderId; }
    bool isFolderExpanded() const { return m_folderExpanded; }

    /** オーディオスレッドとUIスレッド間のプラグインアクセス同期用ミューテックス */
    QMutex& pluginMutex() { return m_pluginMutex; }

    // Setters
    void setName(const QString& name);
    void setInstrumentName(const QString& instrumentName);
    void setVisible(bool visible);
    void setMuted(bool muted);
    void setSolo(bool solo);
    void setVolume(double volume);
    void setPan(double pan);
    void setTimingOffsetMs(double ms);
    void setColor(const QColor& color);

    // フォルダ関連
    void setIsFolder(bool isFolder);
    void setParentFolderId(int folderId);
    void setFolderExpanded(bool expanded);

    // Clip management
    Clip* addClip(qint64 startTick, qint64 durationTicks);
    void removeClip(Clip* clip);
    void clearClips();
    Clip* clipAt(qint64 tick) const;
    Clip* takeClip(Clip* clip);      // クリップを所有権ごと取り出す（削除せず）
    void insertClip(Clip* clip);     // 外部クリップを挿入する

    // プラグイン管理 (インストゥルメント)
    bool loadPlugin(const QString& pluginPath);
    void unloadPlugin();
    void setLoadedPlugin(VST3PluginInstance* instance);

    // エフェクト管理 (FX Slots)
    bool addFxPlugin(const QString& pluginPath);
    void removeFxPlugin(int index);
    void clearFxPlugins();
    VST3PluginInstance* takeFxPlugin(int index);
    void insertFxPlugin(int index, VST3PluginInstance* plugin);
    void moveFxPlugin(int fromIndex, int toIndex);
    const QList<VST3PluginInstance*>& fxPlugins() const { return m_fxPlugins; }

    // シリアライズ
    QJsonObject toJson() const;
    static Track* fromJson(const QJsonObject& json, QObject* parent = nullptr,
                           bool deferPluginRestore = false, bool deferAudioLoad = false);

    // 遅延復元
    bool hasDeferredPluginRestore() const { return m_hasDeferredPluginRestore; }
    void restoreDeferredPlugins();

    static void resetIdCounter() { s_nextId = 1; }

signals:
    void propertyChanged();
    void clipAdded(Clip* clip);
    void clipRemoved(Clip* clip);

private:
    static int s_nextId;
    int m_id;
    QString m_name;
    QString m_instrumentName;
    bool m_visible;
    bool m_muted;
    bool m_solo;
    double m_volume;  // 0.0 - 1.0
    double m_pan;     // -1.0 (left) to 1.0 (right)
    double m_timingOffsetMs; // -100.0 to +100.0 ms
    QColor m_color;   // トラックカラー
    bool m_isFolder;         // フォルダトラックか
    int m_parentFolderId;    // 所属フォルダのTrack ID (-1 = ルートレベル)
    bool m_folderExpanded;   // フォルダが展開されているか
    QList<Clip*> m_clips;
    VST3PluginInstance* m_pluginInstance; // VST3プラグインインスタンス (Instrument)
    QList<VST3PluginInstance*> m_fxPlugins; // FXプラグインインスタンスのリスト
    QMutex m_pluginMutex; // オーディオスレッドとUIスレッド間のプラグインアクセス保護

    // 遅延プラグイン復元用
    bool m_hasDeferredPluginRestore = false;
    QString m_pendingPluginPath;
    QByteArray m_pendingPluginComponentState;
    QByteArray m_pendingPluginControllerState;
    QList<PendingFxState> m_pendingFxStates;

    // デフォルトカラーパレット
    static const QList<QColor>& defaultColors();
};
