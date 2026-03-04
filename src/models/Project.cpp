#include "Project.h"
#include "Track.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDebug>
#include <QCoreApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QMutexLocker>
#include "common/ModelAccessLock.h"

Project::Project(const QString& name, QObject* parent)
    : QObject(parent)
    , m_name(name)
    , m_bpm(DEFAULT_BPM)
    , m_playheadPosition(0)
{
    m_masterTrack = new Track("Master", this);
    connect(m_masterTrack, &Track::propertyChanged, this, &Project::modified);
}

Project::~Project()
{
    clearTracks();
    // m_masterTrack is parented to 'this', so it will be deleted automatically,
    // but we can be explicit if we want.
}

void Project::setName(const QString& name)
{
    if (m_name != name) {
        m_name = name;
        emit nameChanged(m_name);
        emit modified();
    }
}

void Project::setBpm(double bpm)
{
    bpm = qBound(20.0, bpm, 999.0);
    if (!qFuzzyCompare(m_bpm, bpm)) {
        m_bpm = bpm;
        emit bpmChanged(m_bpm);
        emit modified();
    }
}

void Project::setPlayheadPosition(qint64 position)
{
    position = qMax(0LL, position);
    if (m_playheadPosition != position) {
        m_playheadPosition = position;
        emit playheadChanged(m_playheadPosition);
    }
}

Track* Project::addTrack(const QString& name)
{
    QMutexLocker<QRecursiveMutex> locker(&Darwin::modelAccessMutex());

    QString trackName = name.isEmpty() 
        ? QString("Track %1").arg(m_tracks.size() + 1) 
        : name;
    
    Track* track = new Track(trackName, this);
    m_tracks.append(track);
    
    connect(track, &Track::propertyChanged, this, &Project::modified);
    
    emit trackAdded(track);
    emit modified();
    
    return track;
}

void Project::removeTrack(Track* track)
{
    QMutexLocker<QRecursiveMutex> locker(&Darwin::modelAccessMutex());

    if (m_tracks.removeOne(track)) {
        emit trackRemoved(track);
        emit modified();
        track->deleteLater();
    }
}

Track* Project::takeTrack(Track* track)
{
    QMutexLocker<QRecursiveMutex> locker(&Darwin::modelAccessMutex());

    if (m_tracks.removeOne(track)) {
        emit trackRemoved(track);
        emit modified();
        return track;
    }
    return nullptr;
}

void Project::insertTrack(Track* track, int index)
{
    QMutexLocker<QRecursiveMutex> locker(&Darwin::modelAccessMutex());

    if (!track) return;
    track->setParent(this);
    if (index < 0 || index > m_tracks.size()) {
        m_tracks.append(track);
    } else {
        m_tracks.insert(index, track);
    }
    connect(track, &Track::propertyChanged, this, &Project::modified);
    emit trackAdded(track);
    emit modified();
}

void Project::clearTracks()
{
    QMutexLocker<QRecursiveMutex> locker(&Darwin::modelAccessMutex());

    for (Track* track : m_tracks) {
        emit trackRemoved(track);
        track->deleteLater();
    }
    m_tracks.clear();
    emit modified();
}

Track* Project::trackAt(int index) const
{
    if (index >= 0 && index < m_tracks.size()) {
        return m_tracks.at(index);
    }
    return nullptr;
}

Track* Project::trackById(int id) const
{
    for (Track* track : m_tracks) {
        if (track->id() == id) {
            return track;
        }
    }
    return nullptr;
}

int Project::trackIndex(Track* track) const
{
    return m_tracks.indexOf(track);
}

void Project::moveTrack(int fromIndex, int toIndex)
{
    QMutexLocker<QRecursiveMutex> locker(&Darwin::modelAccessMutex());

    if (fromIndex < 0 || fromIndex >= m_tracks.size()) return;
    if (toIndex < 0 || toIndex >= m_tracks.size()) return;
    if (fromIndex == toIndex) return;
    m_tracks.move(fromIndex, toIndex);
    emit trackOrderChanged();
    emit modified();
}

void Project::moveFolderBlock(Track* folder, int toFlatIndex)
{
    QMutexLocker<QRecursiveMutex> locker(&Darwin::modelAccessMutex());

    if (!folder || !folder->isFolder()) return;
    int folderIdx = m_tracks.indexOf(folder);
    if (folderIdx < 0) return;

    // フォルダとその隣接する子孫トラックをブロックとして収集
    QList<Track*> block;
    block.append(folder);
    for (int i = folderIdx + 1; i < m_tracks.size(); ++i) {
        if (isDescendant(m_tracks[i], folder)) {
            block.append(m_tracks[i]);
        } else {
            break;
        }
    }

    // ブロックの先頭が同じ位置なら何もしない
    if (folderIdx == toFlatIndex) return;

    // toFlatIndex より前にあるブロック要素の数を計算
    int countBefore = 0;
    for (Track* t : block) {
        if (m_tracks.indexOf(t) < toFlatIndex) countBefore++;
    }

    // ブロックをリストから削除
    for (Track* t : block) {
        m_tracks.removeOne(t);
    }

    // 調整されたインデックスで再挿入
    int adjustedIdx = qBound(0, toFlatIndex - countBefore, m_tracks.size());
    for (int i = 0; i < block.size(); ++i) {
        m_tracks.insert(adjustedIdx + i, block[i]);
    }

    emit trackOrderChanged();
    emit modified();
}

Track* Project::addFolderTrack(const QString& name)
{
    QMutexLocker<QRecursiveMutex> locker(&Darwin::modelAccessMutex());

    QString folderName = name.isEmpty()
        ? QString("Folder %1").arg(m_tracks.size() + 1)
        : name;

    Track* folder = new Track(folderName, this);
    folder->setIsFolder(true);
    folder->setVolume(1.0);
    m_tracks.append(folder);

    connect(folder, &Track::propertyChanged, this, &Project::modified);

    emit trackAdded(folder);
    emit modified();
    return folder;
}

void Project::addTrackToFolder(Track* track, Track* folder)
{
    QMutexLocker<QRecursiveMutex> locker(&Darwin::modelAccessMutex());

    if (!track || !folder || !folder->isFolder()) return;
    // 循環チェック（自身をその子孫フォルダに入れることはできない）
    Track* ancestor = folder;
    while (ancestor) {
        if (ancestor == track) return; // 循環を発見
        ancestor = folderOf(ancestor);
    }

    // 対象のトラック（フォルダならその全子孫も）をリストから一旦取り出し、新しい親フォルダの子の末尾に挿入する
    QList<Track*> toMove;
    std::function<void(Track*)> collectDescendants = [&](Track* t) {
        if (!toMove.contains(t)) {
            toMove.append(t);
        }
        for (Track* c : m_tracks) {
            if (c->parentFolderId() == t->id() && !toMove.contains(c)) {
                collectDescendants(c);
            }
        }
    };
    collectDescendants(track);

    // 古い位置から削除
    for (Track* t : toMove) {
        m_tracks.removeOne(t);
    }

    track->setParentFolderId(folder->id());

    // 挿入位置を探す（新しい親フォルダの、全ての子孫の直後）
    int folderIdx = m_tracks.indexOf(folder);
    int insertIdx = folderIdx + 1;
    if (folderIdx >= 0) {
        // 全ての子孫をスキップ
        while (insertIdx < m_tracks.size() && isDescendant(m_tracks[insertIdx], folder)) {
            insertIdx++;
        }
    } else {
        insertIdx = m_tracks.size();
    }

    // 挿入
    for (int i = 0; i < toMove.size(); ++i) {
        m_tracks.insert(insertIdx + i, toMove[i]);
    }

    emit folderStructureChanged();
    emit modified();
}

void Project::removeTrackFromFolder(Track* track)
{
    QMutexLocker<QRecursiveMutex> locker(&Darwin::modelAccessMutex());

    if (!track || track->parentFolderId() < 0) return;
    track->setParentFolderId(-1);
    emit folderStructureChanged();
    emit modified();
}

QList<Track*> Project::folderChildren(Track* folder) const
{
    if (!folder || !folder->isFolder()) return {};
    return folderChildren(folder->id());
}

QList<Track*> Project::folderChildren(int folderId) const
{
    // 直下の子トラックのみを返す（再帰的ではない）
    QList<Track*> children;
    for (Track* t : m_tracks) {
        if (t->parentFolderId() == folderId) {
            children.append(t);
        }
    }
    return children;
}

QList<Track*> Project::folderDescendants(Track* folder) const
{
    QList<Track*> descendants;
    if (!folder) return descendants;

    // isDescendant() を使用してすべての子孫を取得する
    for (Track* t : m_tracks) {
        if (isDescendant(t, folder)) {
            descendants.append(t);
        }
    }
    return descendants;
}

Track* Project::folderOf(Track* track) const
{
    if (!track || track->parentFolderId() < 0) return nullptr;
    return trackById(track->parentFolderId());
}

int Project::folderDepth(Track* track) const
{
    int depth = 0;
    Track* f = folderOf(track);
    while (f) {
        depth++;
        f = folderOf(f);
    }
    return depth;
}

bool Project::isTrackVisibleInHierarchy(Track* track) const
{
    if (!track) return false;
    Track* parent = folderOf(track);
    while (parent) {
        if (!parent->isFolderExpanded()) return false;
        parent = folderOf(parent);
    }
    return true;
}

bool Project::isDescendant(Track* child, Track* ancestor) const
{
    if (!child || !ancestor) return false;
    Track* curr = folderOf(child);
    while (curr) {
        if (curr == ancestor) return true;
        curr = folderOf(curr);
    }
    return false;
}

qint64 Project::ticksToMs(qint64 ticks) const
{
    // ms = ticks * 60000 / (bpm * ticksPerBeat)
    return static_cast<qint64>(ticks * 60000.0 / (m_bpm * TICKS_PER_BEAT));
}

qint64 Project::msToTicks(qint64 ms) const
{
    // ticks = ms * bpm * ticksPerBeat / 60000
    return static_cast<qint64>(ms * m_bpm * TICKS_PER_BEAT / 60000.0);
}

double Project::ticksToBeats(qint64 ticks) const
{
    return static_cast<double>(ticks) / TICKS_PER_BEAT;
}

qint64 Project::beatsToTicks(double beats) const
{
    return static_cast<qint64>(beats * TICKS_PER_BEAT);
}

QJsonObject Project::toJson() const
{
    QJsonObject json;
    json["formatVersion"] = 1;
    json["name"] = m_name;
    json["bpm"] = m_bpm;

    QJsonArray tracksArray;
    for (const Track* track : m_tracks) {
        tracksArray.append(track->toJson());
    }
    json["tracks"] = tracksArray;

    if (m_masterTrack) {
        json["masterTrack"] = m_masterTrack->toJson();
    }

    // エクスポート範囲
    if (m_exportStartBar >= 0) json["exportStartBar"] = m_exportStartBar;
    if (m_exportEndBar >= 0)   json["exportEndBar"]   = m_exportEndBar;

    // フラッグ（マーカー）
    if (!m_flags.isEmpty()) {
        QJsonArray flagsArray;
        for (qint64 flag : m_flags) {
            flagsArray.append(static_cast<double>(flag));
        }
        json["flags"] = flagsArray;
    }

    return json;
}

bool Project::fromJson(const QJsonObject& json, bool deferPluginRestore)
{
    QElapsedTimer uiYieldClock;
    uiYieldClock.start();
    auto yieldUi = [&uiYieldClock]() {
        if (uiYieldClock.elapsed() >= 8) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 2);
            uiYieldClock.restart();
        }
    };

    // バージョンチェック
    int version = json["formatVersion"].toInt(0);
    if (version < 1) {
        qWarning() << "Project: 不明なフォーマットバージョン:" << version;
        return false;
    }

    // 既存データをクリア
    clearTracks();
    if (m_masterTrack) {
        m_masterTrack->deleteLater();
        m_masterTrack = nullptr;
    }
    Track::resetIdCounter();
    yieldUi();

    // 基本情報を復元
    m_name = json["name"].toString("Untitled");
    emit nameChanged(m_name);

    m_bpm = json["bpm"].toDouble(DEFAULT_BPM);
    emit bpmChanged(m_bpm);

    // Masterを復元 (フォールバック)
    if (json.contains("masterTrack")) {
        m_masterTrack = Track::fromJson(json["masterTrack"].toObject(), this, deferPluginRestore);
    } else {
        m_masterTrack = new Track("Master", this);
    }
    connect(m_masterTrack, &Track::propertyChanged, this, &Project::modified);
    yieldUi();

    // トラックを復元
    QJsonArray tracksArray = json["tracks"].toArray();
    for (const QJsonValue& val : tracksArray) {
        Track* track = Track::fromJson(val.toObject(), this, deferPluginRestore);
        m_tracks.append(track);
        connect(track, &Track::propertyChanged, this, &Project::modified);
        emit trackAdded(track);
        yieldUi();
    }

    m_playheadPosition = 0;
    emit playheadChanged(0);

    // エクスポート範囲を復元
    m_exportStartBar = json["exportStartBar"].toDouble(-1.0);
    m_exportEndBar = json["exportEndBar"].toDouble(-1.0);
    emit exportRangeChanged();

    // フラッグを復元
    m_flags.clear();
    if (json.contains("flags")) {
        QJsonArray flagsArray = json["flags"].toArray();
        for (const QJsonValue& val : flagsArray) {
            m_flags.append(static_cast<qint64>(val.toDouble()));
        }
        std::sort(m_flags.begin(), m_flags.end());
    }
    emit flagsChanged();

    emit modified();
    return true;
}

void Project::setExportStartBar(double bar)
{
    if (!qFuzzyCompare(m_exportStartBar, bar)) {
        m_exportStartBar = bar;
        emit exportRangeChanged();
        emit modified();
    }
}

void Project::setExportEndBar(double bar)
{
    if (!qFuzzyCompare(m_exportEndBar, bar)) {
        m_exportEndBar = bar;
        emit exportRangeChanged();
        emit modified();
    }
}

qint64 Project::exportStartTick() const
{
    if (m_exportStartBar < 0) return 0;
    // 小節位置(double) × 4拍 × TICKS_PER_BEAT
    return static_cast<qint64>(m_exportStartBar * 4.0 * TICKS_PER_BEAT);
}

qint64 Project::exportEndTick() const
{
    if (m_exportEndBar < 0) return -1; // -1 = 自動（全クリップ末尾）
    return static_cast<qint64>(m_exportEndBar * 4.0 * TICKS_PER_BEAT);
}

// ===== フラッグ（マーカー）管理 =====

void Project::addFlag(qint64 tickPosition)
{
    if (tickPosition < 0) return;
    // 既に同じ位置にある場合は追加しない
    if (m_flags.contains(tickPosition)) return;
    m_flags.append(tickPosition);
    std::sort(m_flags.begin(), m_flags.end());
    emit flagsChanged();
    emit modified();
}

void Project::removeFlag(qint64 tickPosition)
{
    if (m_flags.removeOne(tickPosition)) {
        emit flagsChanged();
        emit modified();
    }
}

void Project::clearFlags()
{
    if (!m_flags.isEmpty()) {
        m_flags.clear();
        emit flagsChanged();
        emit modified();
    }
}

bool Project::hasFlag(qint64 tickPosition) const
{
    return m_flags.contains(tickPosition);
}

qint64 Project::nextFlag(qint64 currentTick) const
{
    for (qint64 flag : m_flags) {
        if (flag > currentTick) {
            return flag;
        }
    }
    return -1; // 右側にフラッグなし
}

qint64 Project::prevFlag(qint64 currentTick) const
{
    // ソート済みリストを逆順に走査
    for (int i = m_flags.size() - 1; i >= 0; --i) {
        if (m_flags[i] < currentTick) {
            return m_flags[i];
        }
    }
    return -1; // 左側にフラッグなし
}

void Project::setGridSnapEnabled(bool enabled)
{
    if (m_gridSnapEnabled != enabled) {
        m_gridSnapEnabled = enabled;
        emit gridSnapChanged(m_gridSnapEnabled);
    }
}

bool Project::saveToFile(const QString& filePath)
{
    QJsonDocument doc(toJson());
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Project: ファイルを開けません（書き込み）:" << filePath;
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    m_filePath = filePath;
    qDebug() << "Project: 保存完了:" << filePath;
    return true;
}

bool Project::loadFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Project: ファイルを開けません（読み込み）:" << filePath;
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Project: JSONパースエラー:" << parseError.errorString();
        return false;
    }

    if (!fromJson(doc.object())) {
        return false;
    }

    m_filePath = filePath;
    qDebug() << "Project: 読み込み完了:" << filePath;
    return true;
}
