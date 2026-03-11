#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

class Track;

/**
 * @brief プロジェクト全体を管理するクラス
 */
class Project : public QObject
{
    Q_OBJECT

public:
    static const int TICKS_PER_BEAT = 480;
    static const int DEFAULT_BPM = 128;

    explicit Project(const QString& name = "Untitled", QObject* parent = nullptr);
    ~Project() override;

    // Getters
    QString name() const { return m_name; }
    double bpm() const { return m_bpm; }
    qint64 playheadPosition() const { return m_playheadPosition; }
    const QList<Track*>& tracks() const { return m_tracks; }
    int trackCount() const { return m_tracks.size(); }
    Track* masterTrack() const { return m_masterTrack; }

    // Setters
    void setName(const QString& name);
    void setBpm(double bpm);
    void setPlayheadPosition(qint64 position);

    // Track management
    Track* addTrack(const QString& name = QString());
    Track* addFolderTrack(const QString& name = QString());
    void removeTrack(Track* track);
    Track* takeTrack(Track* track);                      // 追加
    void insertTrack(Track* track, int index = -1);     // 追加
    void clearTracks();
    Track* trackAt(int index) const;
    Track* trackById(int id) const;
    int trackIndex(Track* track) const;
    void moveTrack(int fromIndex, int toIndex);
    void moveFolderBlock(Track* folder, int toFlatIndex);

    // フォルダ管理
    void addTrackToFolder(Track* track, Track* folder);
    void removeTrackFromFolder(Track* track);
    QList<Track*> folderChildren(Track* folder) const;
    QList<Track*> folderChildren(int folderId) const;
    QList<Track*> folderDescendants(Track* folder) const;
    Track* folderOf(Track* track) const;
    int folderDepth(Track* track) const;
    bool isTrackVisibleInHierarchy(Track* track) const;
    bool isDescendant(Track* child, Track* ancestor) const;

    // Utility
    qint64 ticksToMs(qint64 ticks) const;
    qint64 msToTicks(qint64 ms) const;
    double ticksToBeats(qint64 ticks) const;
    qint64 beatsToTicks(double beats) const;

    // エクスポート範囲（小節位置 double、-1 = 自動）
    double exportStartBar() const { return m_exportStartBar; }
    double exportEndBar() const { return m_exportEndBar; }
    void setExportStartBar(double bar);
    void setExportEndBar(double bar);
    qint64 exportStartTick() const;
    qint64 exportEndTick() const;

    // フラッグ（マーカー）管理
    const QList<qint64>& flags() const { return m_flags; }
    void addFlag(qint64 tickPosition);
    void removeFlag(qint64 tickPosition);
    void clearFlags();
    bool hasFlag(qint64 tickPosition) const;
    qint64 nextFlag(qint64 currentTick) const;   // 現在位置より右のフラッグ（-1 = なし）
    qint64 prevFlag(qint64 currentTick) const;   // 現在位置より左のフラッグ（-1 = なし）

    // グリッドスナップ
    bool gridSnapEnabled() const { return m_gridSnapEnabled; }
    void setGridSnapEnabled(bool enabled);

    // シリアライズ
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& json, bool deferPluginRestore = false,
                  bool deferAudioLoad = false);
    bool saveToFile(const QString& filePath);
    bool loadFromFile(const QString& filePath);
    QString currentFilePath() const { return m_filePath; }
    void setFilePath(const QString& path) { m_filePath = path; }

signals:
    void nameChanged(const QString& name);
    void bpmChanged(double bpm);
    void playheadChanged(qint64 position);
    void trackAdded(Track* track);
    void trackRemoved(Track* track);
    void trackOrderChanged();
    void folderStructureChanged();
    void exportRangeChanged();
    void flagsChanged();
    void gridSnapChanged(bool enabled);
    void modified();

private:
    QString m_name;
    double m_bpm;
    qint64 m_playheadPosition;  // In ticks
    QList<Track*> m_tracks;
    Track* m_masterTrack;
    QString m_filePath;  // 現在のプロジェクトファイルパス
    double m_exportStartBar = -1.0;
    double m_exportEndBar = -1.0;
    QList<qint64> m_flags;  // フラッグ位置リスト（ティック単位、ソート済み）
    bool m_gridSnapEnabled = true;  // グリッドスナップ有効/無効
};
