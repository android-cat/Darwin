/**
 * @file ArrangementGridWidget_DragDrop.cpp
 * @brief ArrangementGridWidget のファイル ドラッグ&ドロップ処理（MIDI / オーディオ）
 */
#include "ArrangementGridWidget.h"
#include "models/Project.h"
#include "models/Track.h"
#include "models/Clip.h"
#include "models/Note.h"
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QDebug>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include "common/Constants.h"
#include "common/MidiFileParser.h"
#include "common/AudioFileReader.h"

using namespace Darwin;

void ArrangementGridWidget::dragEnterEvent(QDragEnterEvent *event)
{
    // MIMEデータにファイルURLが含まれているか確認
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            QString path = url.toLocalFile().toLower();
            if (path.endsWith(".mid") || path.endsWith(".midi") ||
                AudioFileReader::isSupportedAudioFile(path)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void ArrangementGridWidget::dragMoveEvent(QDragMoveEvent *event)
{
    // ドラッグ中もアクセプトし続ける
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void ArrangementGridWidget::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls()) return;

    for (const QUrl& url : event->mimeData()->urls()) {
        QString filePath = url.toLocalFile();
        QString lower = filePath.toLower();
        if (lower.endsWith(".mid") || lower.endsWith(".midi")) {
            handleMidiFileDrop(filePath, event->position().toPoint());
        } else if (AudioFileReader::isSupportedAudioFile(lower)) {
            handleAudioFileDrop(filePath, event->position().toPoint());
        }
    }

    event->acceptProposedAction();
}

void ArrangementGridWidget::handleMidiFileDrop(const QString& filePath, const QPoint& dropPos)
{
    if (!m_project) return;

    // MIDIファイルをパース
    MidiFileParser::ParseResult midi = MidiFileParser::parse(filePath, Project::TICKS_PER_BEAT);
    if (!midi.success || midi.notes.isEmpty()) {
        qWarning() << "MIDIファイルのパースに失敗:" << midi.errorMessage;
        return;
    }

    // トラック/クリップ作成中の中間再描画を防止
    setUpdatesEnabled(false);

    const int rowHeight = ARRANGEMENT_TRACK_ROW_HEIGHT;
    QList<Track*> visTracks_ = visibleTracks();
    int trackIndex = dropPos.y() / rowHeight;

    // ドロップ位置のX座標からスタートtickを算出（小節単位にスナップ）
    qint64 dropTick = static_cast<qint64>(dropPos.x() / pixelsPerTick());
    dropTick = (dropTick / TICKS_PER_BAR) * TICKS_PER_BAR;

    Track* targetTrack = nullptr;

    // 該当トラックが存在する場合はそのトラックを使用
    if (trackIndex >= 0 && trackIndex < visTracks_.size()) {
        Track* t = visTracks_.at(trackIndex);
        if (!t->isFolder()) {
            targetTrack = t;
        }
    }

    // トラックが存在しない場合は新規作成（MIDIファイル名をトラック名に）
    if (!targetTrack) {
        targetTrack = m_project->addTrack(midi.fileName);
    }

    if (!targetTrack) return;

    // MIDIノートの最小/最大tickを取得してクリップの範囲を決定
    qint64 minTick = midi.notes.first().startTick;
    qint64 maxTick = 0;
    for (const auto& note : midi.notes) {
        qint64 noteEnd = note.startTick + note.durationTicks;
        if (noteEnd > maxTick) maxTick = noteEnd;
    }

    // クリップの長さを計算（小節単位に切り上げ）
    qint64 rawDuration = maxTick - minTick;
    qint64 clipDuration = ((rawDuration + TICKS_PER_BAR - 1) / TICKS_PER_BAR) * TICKS_PER_BAR;
    if (clipDuration <= 0) clipDuration = TICKS_PER_BAR;

    // クリップを作成
    Clip* clip = targetTrack->addClip(dropTick, clipDuration);

    // ノートをクリップに追加（クリップ内相対tickに変換）
    for (const auto& midiNote : midi.notes) {
        qint64 relativeTick = midiNote.startTick - minTick;
        clip->addNote(midiNote.pitch, relativeTick, midiNote.durationTicks, midiNote.velocity);
    }

    // ドロップ地点からクリップ形状に広がる波リビールアニメーションを開始
    startWaveReveal(clip->id(), QPointF(dropPos));

    // 選択状態にする
    m_selectedClipId = clip->id();
    emit clipSelected(clip);

    // 再描画を再有効化（波リビールが登録済みなので安全）
    setUpdatesEnabled(true);

    qDebug() << QStringLiteral("MIDIファイルをインポート: %1 (%2 ノート → トラック '%3')")
                .arg(midi.fileName)
                .arg(midi.notes.size())
                .arg(targetTrack->name());

    update();
}

void ArrangementGridWidget::handleAudioFileDrop(const QString& filePath, const QPoint& dropPos)
{
    if (!m_project) return;

    // オーディオファイル名を取得
    QFileInfo fileInfo(filePath);
    const QString fileName = fileInfo.baseName();

    const int rowHeight = ARRANGEMENT_TRACK_ROW_HEIGHT;
    const QList<Track*>& visTracks = visibleTracks();
    int trackIndex = dropPos.y() / rowHeight;

    // ドロップ位置のX座標からスタートtickを算出（小節単位にスナップ）
    qint64 dropTick = static_cast<qint64>(dropPos.x() / pixelsPerTick());
    dropTick = (dropTick / TICKS_PER_BAR) * TICKS_PER_BAR;

    // 該当トラックが存在する場合はそのトラックを使用
    QPointer<Track> targetTrack;
    if (trackIndex >= 0 && trackIndex < visTracks.size()) {
        Track* t = visTracks.at(trackIndex);
        if (!t->isFolder()) {
            targetTrack = t;
        }
    }

    QPointer<Project> droppedProject = m_project;
    auto* watcher = new QFutureWatcher<AudioFileData>(this);
    connect(watcher, &QFutureWatcher<AudioFileData>::finished, this,
            [this, watcher, droppedProject, targetTrack, filePath, fileName, dropTick, dropPos]() {
        const AudioFileData audioData = watcher->result();
        watcher->deleteLater();

        if (!droppedProject || droppedProject != m_project) {
            return;
        }

        if (!audioData.valid) {
            qWarning() << "オーディオファイル読み込みエラー:" << audioData.errorMessage;
            return;
        }

        Track* resolvedTrack = targetTrack.data();
        if (!resolvedTrack) {
            resolvedTrack = m_project->addTrack(fileName);
        }
        if (!resolvedTrack) {
            return;
        }

        // 読み込みと波形プレビュー生成はワーカースレッドで済ませ、UIスレッドではモデル更新だけ行う。
        const double durationSeconds = static_cast<double>(audioData.samplesL.size()) / audioData.sampleRate;
        const double ticksPerSecond = m_project->bpm() * TICKS_PER_BEAT / 60.0;
        qint64 rawDuration = static_cast<qint64>(durationSeconds * ticksPerSecond);
        qint64 clipDuration = ((rawDuration + TICKS_PER_BAR - 1) / TICKS_PER_BAR) * TICKS_PER_BAR;
        if (clipDuration <= 0) {
            clipDuration = TICKS_PER_BAR;
        }

        Clip* clip = resolvedTrack->addClip(dropTick, clipDuration);
        if (!clip) {
            return;
        }

        clip->setAudioData(audioData.samplesL, audioData.samplesR,
                           audioData.sampleRate, filePath, audioData.waveformPreview);
        startWaveReveal(clip->id(), QPointF(dropPos));

        m_selectedClipId = clip->id();
        emit clipSelected(clip);

        qDebug() << QStringLiteral("オーディオファイルをインポート: %1 (%.1f秒 → トラック '%2')")
                    .arg(fileName)
                    .arg(durationSeconds)
                    .arg(resolvedTrack->name());

        update();
    });
    watcher->setFuture(QtConcurrent::run([filePath]() {
        return AudioFileReader::readFile(filePath);
    }));
}
