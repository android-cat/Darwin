#include "UndoCommands.h"
#include "Project.h"
#include "Track.h"
#include "Clip.h"
#include "Note.h"
#include "VST3PluginInstance.h"
#include "common/Constants.h"

#include <QDebug>

using namespace Darwin;

// ===== ノート追加コマンド =====

AddNoteCommand::AddNoteCommand(Clip* clip, int pitch, qint64 startTick,
                               qint64 durationTicks, int velocity,
                               QUndoCommand* parent)
    : QUndoCommand("Add Note", parent)
    , m_clip(clip)
    , m_note(nullptr)
    , m_pitch(pitch)
    , m_startTick(startTick)
    , m_durationTicks(durationTicks)
    , m_velocity(velocity)
    , m_ownsNote(false)
    , m_firstRedo(true)
{
}

AddNoteCommand::~AddNoteCommand()
{
    if (m_ownsNote && m_note) {
        delete m_note;
    }
}

void AddNoteCommand::undo()
{
    if (m_note && m_clip) {
        m_clip->takeNote(m_note);
        m_ownsNote = true;
    }
}

void AddNoteCommand::redo()
{
    if (m_firstRedo) {
        if (m_clip) {
            m_note = m_clip->addNote(m_pitch, m_startTick, m_durationTicks, m_velocity);
            m_ownsNote = false;
        }
        m_firstRedo = false;
    } else {
        if (m_clip && m_note) {
            m_clip->insertNote(m_note);
            m_ownsNote = false;
        }
    }
}

// ===== ノート削除コマンド =====

RemoveNoteCommand::RemoveNoteCommand(Clip* clip, Note* note,
                                     QUndoCommand* parent)
    : QUndoCommand("Remove Note", parent)
    , m_clip(clip)
    , m_note(note)
    , m_ownsNote(false)
{
}

RemoveNoteCommand::~RemoveNoteCommand()
{
    if (m_ownsNote && m_note) {
        delete m_note;
    }
}

void RemoveNoteCommand::undo()
{
    if (m_clip && m_note) {
        m_clip->insertNote(m_note);
        m_ownsNote = false;
    }
}

void RemoveNoteCommand::redo()
{
    if (m_clip && m_note) {
        m_clip->takeNote(m_note);
        m_ownsNote = true;
    }
}

// ===== ノート移動コマンド =====
// (既存のまま)
MoveNoteCommand::MoveNoteCommand(Note* note, int newPitch,
                                 qint64 newStartTick, QUndoCommand* parent)
    : QUndoCommand("Move Note", parent)
    , m_note(note)
    , m_oldPitch(note->pitch())
    , m_oldStartTick(note->startTick())
    , m_newPitch(newPitch)
    , m_newStartTick(newStartTick)
{
}

void MoveNoteCommand::undo()
{
    m_note->setPitch(m_oldPitch);
    m_note->setStartTick(m_oldStartTick);
}

void MoveNoteCommand::redo()
{
    m_note->setPitch(m_newPitch);
    m_note->setStartTick(m_newStartTick);
}

bool MoveNoteCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) return false;
    auto* cmd = static_cast<const MoveNoteCommand*>(other);
    if (cmd->m_note != m_note) return false;
    m_newPitch = cmd->m_newPitch;
    m_newStartTick = cmd->m_newStartTick;
    return true;
}

// ===== ノートリサイズコマンド =====

ResizeNoteCommand::ResizeNoteCommand(Note* note, qint64 newDurationTicks,
                                     QUndoCommand* parent)
    : QUndoCommand("Resize Note", parent)
    , m_note(note)
    , m_oldDurationTicks(note->durationTicks())
    , m_newDurationTicks(newDurationTicks)
{
}

void ResizeNoteCommand::undo()
{
    m_note->setDurationTicks(m_oldDurationTicks);
}

void ResizeNoteCommand::redo()
{
    m_note->setDurationTicks(m_newDurationTicks);
}

bool ResizeNoteCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) return false;
    auto* cmd = static_cast<const ResizeNoteCommand*>(other);
    if (cmd->m_note != m_note) return false;
    m_newDurationTicks = cmd->m_newDurationTicks;
    return true;
}

// ===== ベロシティ変更コマンド =====

ChangeVelocityCommand::ChangeVelocityCommand(Note* note, int newVelocity,
                                             QUndoCommand* parent)
    : QUndoCommand("Change Velocity", parent)
    , m_note(note)
    , m_oldVelocity(note->velocity())
    , m_newVelocity(newVelocity)
{
}

void ChangeVelocityCommand::undo()
{
    m_note->setVelocity(m_oldVelocity);
}

void ChangeVelocityCommand::redo()
{
    m_note->setVelocity(m_newVelocity);
}

bool ChangeVelocityCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) return false;
    auto* cmd = static_cast<const ChangeVelocityCommand*>(other);
    if (cmd->m_note != m_note) return false;
    m_newVelocity = cmd->m_newVelocity;
    return true;
}

// ===== クリップ追加コマンド =====

AddClipCommand::AddClipCommand(Track* track, qint64 startTick,
                               qint64 durationTicks, QUndoCommand* parent)
    : QUndoCommand("Add Clip", parent)
    , m_track(track)
    , m_clip(nullptr)
    , m_startTick(startTick)
    , m_durationTicks(durationTicks)
    , m_ownsClip(false)
    , m_firstRedo(true)
{
}

AddClipCommand::~AddClipCommand()
{
    if (m_ownsClip && m_clip) {
        delete m_clip;
    }
}

void AddClipCommand::undo()
{
    if (m_clip && m_track) {
        m_track->takeClip(m_clip);
        m_ownsClip = true;
    }
}

void AddClipCommand::redo()
{
    if (m_firstRedo) {
        if (m_track) {
            m_clip = m_track->addClip(m_startTick, m_durationTicks);
            m_ownsClip = false;
        }
        m_firstRedo = false;
    } else {
        if (m_track && m_clip) {
            m_track->insertClip(m_clip);
            m_ownsClip = false;
        }
    }
}

// ===== 既存クリップ採用コマンド =====

AdoptClipCommand::AdoptClipCommand(Track* track, Clip* clip,
                                   QUndoCommand* parent)
    : QUndoCommand("Record Clip", parent)
    , m_track(track)
    , m_clip(clip)
    , m_ownsClip(false)
    , m_firstRedo(true)
{
}

AdoptClipCommand::~AdoptClipCommand()
{
    if (m_ownsClip && m_clip) {
        delete m_clip;
    }
}

void AdoptClipCommand::undo()
{
    if (m_track && m_clip) {
        m_track->takeClip(m_clip);
        m_ownsClip = true;
    }
}

void AdoptClipCommand::redo()
{
    if (m_firstRedo) {
        // 録音直後のクリップ実体をそのまま採用するため、
        // 初回redoでは何も生成せず「既に存在している」状態を正とする。
        m_firstRedo = false;
        return;
    }

    if (m_track && m_clip) {
        m_track->insertClip(m_clip);
        m_ownsClip = false;
    }
}

// ===== クリップ削除コマンド =====

RemoveClipCommand::RemoveClipCommand(Track* track, Clip* clip,
                                     QUndoCommand* parent)
    : QUndoCommand("Remove Clip", parent)
    , m_track(track)
    , m_clip(clip)
    , m_ownsClip(false)
{
}

RemoveClipCommand::~RemoveClipCommand()
{
    if (m_ownsClip && m_clip) {
        delete m_clip;
    }
}

void RemoveClipCommand::undo()
{
    if (m_track && m_clip) {
        m_track->insertClip(m_clip);
        m_ownsClip = false;
    }
}

void RemoveClipCommand::redo()
{
    if (m_track && m_clip) {
        m_track->takeClip(m_clip);
        m_ownsClip = true;
    }
}

// ===== クリップ移動コマンド =====

MoveClipCommand::MoveClipCommand(Clip* clip, qint64 newStartTick,
                                 QUndoCommand* parent)
    : QUndoCommand("Move Clip", parent)
    , m_clip(clip)
    , m_oldStartTick(clip->startTick())
    , m_newStartTick(newStartTick)
{
}

void MoveClipCommand::undo()
{
    m_clip->setStartTick(m_oldStartTick);
}

void MoveClipCommand::redo()
{
    m_clip->setStartTick(m_newStartTick);
}

bool MoveClipCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) return false;
    auto* cmd = static_cast<const MoveClipCommand*>(other);
    if (cmd->m_clip != m_clip) return false;
    m_newStartTick = cmd->m_newStartTick;
    return true;
}

// ===== クリップのトラック間移動コマンド =====

MoveClipToTrackCommand::MoveClipToTrackCommand(Clip* clip, Track* srcTrack, Track* dstTrack,
                                               QUndoCommand* parent)
    : QUndoCommand("Move Clip to Track", parent)
    , m_clip(clip)
    , m_srcTrack(srcTrack)
    , m_dstTrack(dstTrack)
    , m_firstRedo(true)
{
}

void MoveClipToTrackCommand::undo()
{
    if (m_dstTrack && m_srcTrack && m_clip) {
        m_dstTrack->takeClip(m_clip);
        m_srcTrack->insertClip(m_clip);
    }
}

void MoveClipToTrackCommand::redo()
{
    if (m_firstRedo) {
        // 初回は既に移動済みなのでスキップ
        m_firstRedo = false;
        return;
    }
    if (m_srcTrack && m_dstTrack && m_clip) {
        m_srcTrack->takeClip(m_clip);
        m_dstTrack->insertClip(m_clip);
    }
}

// ===== クリップリサイズコマンド =====

ResizeClipCommand::ResizeClipCommand(Clip* clip, qint64 newDurationTicks,
                                     QUndoCommand* parent)
    : QUndoCommand("Resize Clip", parent)
    , m_clip(clip)
    , m_oldDurationTicks(clip->durationTicks())
    , m_newDurationTicks(newDurationTicks)
{
}

void ResizeClipCommand::undo()
{
    m_clip->setDurationTicks(m_oldDurationTicks);
}

void ResizeClipCommand::redo()
{
    m_clip->setDurationTicks(m_newDurationTicks);
}

bool ResizeClipCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) return false;
    auto* cmd = static_cast<const ResizeClipCommand*>(other);
    if (cmd->m_clip != m_clip) return false;
    m_newDurationTicks = cmd->m_newDurationTicks;
    return true;
}

// ===== トラック追加コマンド =====

AddTrackCommand::AddTrackCommand(Project* project, const QString& name,
                                 QUndoCommand* parent)
    : QUndoCommand("Add Track", parent)
    , m_project(project)
    , m_track(nullptr)
    , m_name(name)
    , m_ownsTrack(false)
    , m_firstRedo(true)
{
}

AddTrackCommand::~AddTrackCommand()
{
    if (m_ownsTrack && m_track) {
        delete m_track;
    }
}

void AddTrackCommand::undo()
{
    if (m_track && m_project) {
        m_project->takeTrack(m_track);
        m_ownsTrack = true;
    }
}

void AddTrackCommand::redo()
{
    if (m_firstRedo) {
        if (m_project) {
            m_track = m_project->addTrack(m_name);
            m_ownsTrack = false;
        }
        m_firstRedo = false;
    } else {
        if (m_project && m_track) {
            m_project->insertTrack(m_track);
            m_ownsTrack = false;
        }
    }
}

// ===== 既存トラック採用コマンド =====

AdoptTrackCommand::AdoptTrackCommand(Project* project, Track* track,
                                     QUndoCommand* parent)
    : QUndoCommand("Record Track", parent)
    , m_project(project)
    , m_track(track)
    , m_trackIndex(project ? project->trackIndex(track) : -1)
    , m_ownsTrack(false)
    , m_firstRedo(true)
{
}

AdoptTrackCommand::~AdoptTrackCommand()
{
    if (m_ownsTrack && m_track) {
        delete m_track;
    }
}

void AdoptTrackCommand::undo()
{
    if (m_project && m_track) {
        m_project->takeTrack(m_track);
        m_ownsTrack = true;
    }
}

void AdoptTrackCommand::redo()
{
    if (m_firstRedo) {
        // 録音開始時に自動作成されたトラック実体を採用するので、
        // 初回redoでは追加済みオブジェクトをそのまま使う。
        m_firstRedo = false;
        return;
    }

    if (m_project && m_track) {
        m_project->insertTrack(m_track, m_trackIndex);
        m_ownsTrack = false;
    }
}

// ===== トラック削除コマンド =====

RemoveTrackCommand::RemoveTrackCommand(Project* project, Track* track,
                                       QUndoCommand* parent)
    : QUndoCommand("Remove Track", parent)
    , m_project(project)
    , m_track(track)
    , m_trackIndex(project->tracks().indexOf(track))
    , m_ownsTrack(false)
{
}

RemoveTrackCommand::~RemoveTrackCommand()
{
    if (m_ownsTrack && m_track) {
        delete m_track;
    }
}

void RemoveTrackCommand::undo()
{
    if (m_project && m_track) {
        m_project->insertTrack(m_track, m_trackIndex);
        m_ownsTrack = false;
    }
}

void RemoveTrackCommand::redo()
{
    if (m_project && m_track) {
        m_project->takeTrack(m_track);
        m_ownsTrack = true;
    }
}

// ===== クリップ分割コマンド =====

SplitClipCommand::SplitClipCommand(Track* track, Clip* clip, qint64 relSplitTick,
                                   double bpm, QUndoCommand* parent)
    : QUndoCommand("Split Clip", parent)
    , m_track(track)
    , m_clip(clip)
    , m_newClip(nullptr)
    , m_relSplitTick(relSplitTick)
    , m_bpm(bpm)
    , m_origDuration(clip->durationTicks())
    , m_isAudioClip(clip->isAudioClip())
    , m_audioSampleRate(clip->audioSampleRate())
    , m_audioFilePath(clip->audioFilePath())
    , m_ownsNewClip(false)
    , m_ownsMoved(false)
    , m_firstRedo(true)
{
    // オーディオの場合、分割前にフルデータを保存
    if (m_isAudioClip) {
        m_origFullAudioL = clip->audioSamplesL();
        m_origFullAudioR = clip->audioSamplesR();

        double ticksPerSecond = m_bpm * TICKS_PER_BEAT / 60.0;
        double splitSeconds = static_cast<double>(relSplitTick) / ticksPerSecond;
        qint64 splitSample = static_cast<qint64>(splitSeconds * m_audioSampleRate);

        m_firstHalfAudioL = m_origFullAudioL.mid(0, static_cast<int>(splitSample));
        m_firstHalfAudioR = m_origFullAudioR.mid(0, static_cast<int>(splitSample));
        if (splitSample < m_origFullAudioL.size()) {
            m_secondHalfAudioL = m_origFullAudioL.mid(static_cast<int>(splitSample));
            m_secondHalfAudioR = m_origFullAudioR.mid(static_cast<int>(splitSample));
        }
    }
}

SplitClipCommand::~SplitClipCommand()
{
    if (m_ownsNewClip && m_newClip) {
        delete m_newClip;
    }
    if (m_ownsMoved) {
        qDeleteAll(m_movedNotes);
    }
}

void SplitClipCommand::undo()
{
    if (!m_track || !m_clip || !m_newClip) return;

    // 新クリップのノートをすべて削除（redo時にaddNoteで新規生成したもの）
    if (!m_isAudioClip) {
        QList<Note*> newNotes = m_newClip->notes();
        for (Note* note : newNotes) {
            m_newClip->takeNote(note);
            delete note;
        }
    }

    // 新クリップをトラックから取り出す
    m_track->takeClip(m_newClip);
    m_ownsNewClip = true;

    // 移動したノートを元クリップに戻す
    for (Note* note : m_movedNotes) {
        m_clip->insertNote(note);
    }
    m_ownsMoved = false;

    // トリムされたノートの長さを復元
    for (const auto& tn : m_truncatedNotes) {
        tn.note->setDurationTicks(tn.origDuration);
    }

    // 元クリップのdurationを復元
    m_clip->setDurationTicks(m_origDuration);

    // オーディオデータを元に戻す
    if (m_isAudioClip) {
        m_clip->setAudioData(m_origFullAudioL, m_origFullAudioR,
                             m_audioSampleRate, m_audioFilePath);
    }
}

void SplitClipCommand::redo()
{
    if (!m_track || !m_clip) return;

    if (m_firstRedo) {
        // ── 初回: 分割を実行 ──
        qint64 origStart = m_clip->startTick();
        qint64 newStart = origStart + m_relSplitTick;
        qint64 newDuration = m_origDuration - m_relSplitTick;

        m_newClip = m_track->addClip(newStart, newDuration);

        if (m_isAudioClip) {
            m_newClip->setAudioData(m_secondHalfAudioL, m_secondHalfAudioR,
                                    m_audioSampleRate, m_audioFilePath);
            m_clip->setAudioData(m_firstHalfAudioL, m_firstHalfAudioR,
                                 m_audioSampleRate, m_audioFilePath);
        } else {
            // MIDIノート分割
            m_movedNotes.clear();
            m_truncatedNotes.clear();
            m_newClipNoteSnapshots.clear();

            QList<Note*> toMove;
            for (Note* note : m_clip->notes()) {
                qint64 noteStart = note->startTick();
                qint64 noteEnd = noteStart + note->durationTicks();

                if (noteStart >= m_relSplitTick) {
                    // ノート全体が後半 → 移動対象
                    m_newClipNoteSnapshots.append({
                        note->pitch(),
                        noteStart - m_relSplitTick,
                        note->durationTicks(),
                        note->velocity()
                    });
                    toMove.append(note);
                } else if (noteEnd > m_relSplitTick) {
                    // 分割点をまたぐノート
                    qint64 firstHalfDuration = m_relSplitTick - noteStart;
                    qint64 secondHalfDuration = noteEnd - m_relSplitTick;

                    m_truncatedNotes.append({ note, note->durationTicks() });
                    note->setDurationTicks(firstHalfDuration);

                    m_newClipNoteSnapshots.append({
                        note->pitch(), 0, secondHalfDuration, note->velocity()
                    });
                }
            }
            // 後半のノートを元クリップから取り出し
            for (Note* note : toMove) {
                m_clip->takeNote(note);
                m_movedNotes.append(note);
            }
            m_ownsMoved = true;

            // 新クリップにノートを作成
            for (const auto& snap : m_newClipNoteSnapshots) {
                m_newClip->addNote(snap.pitch, snap.startTick,
                                   snap.durationTicks, snap.velocity);
            }
        }

        m_clip->setDurationTicks(m_relSplitTick);
        m_ownsNewClip = false;
        m_firstRedo = false;
    } else {
        // ── 2回目以降: 保存済み状態から復元 ──

        // ノートを再度分割
        if (!m_isAudioClip) {
            // 移動ノートを元クリップから取り出し
            for (Note* note : m_movedNotes) {
                m_clip->takeNote(note);
            }
            m_ownsMoved = true;

            // スパニングノートを再トリム
            for (const auto& tn : m_truncatedNotes) {
                qint64 firstHalfDuration = m_relSplitTick - tn.note->startTick();
                tn.note->setDurationTicks(firstHalfDuration);
            }

            // 新クリップにノートを再作成
            for (const auto& snap : m_newClipNoteSnapshots) {
                m_newClip->addNote(snap.pitch, snap.startTick,
                                   snap.durationTicks, snap.velocity);
            }
        }

        // オーディオデータを再分割
        if (m_isAudioClip) {
            m_clip->setAudioData(m_firstHalfAudioL, m_firstHalfAudioR,
                                 m_audioSampleRate, m_audioFilePath);
            m_newClip->setAudioData(m_secondHalfAudioL, m_secondHalfAudioR,
                                    m_audioSampleRate, m_audioFilePath);
        }

        m_clip->setDurationTicks(m_relSplitTick);

        // 新クリップをトラックに再挿入
        m_track->insertClip(m_newClip);
        m_ownsNewClip = false;
    }
}
