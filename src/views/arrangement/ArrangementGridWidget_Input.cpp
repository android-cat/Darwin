/**
 * @file ArrangementGridWidget_Input.cpp
 * @brief ArrangementGridWidget のマウス・キー入力処理
 */
#include "ArrangementGridWidget.h"
#include "models/Project.h"
#include "models/Track.h"
#include "models/Clip.h"
#include "commands/UndoCommands.h"
#include "models/Note.h"
#include <QPainter>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QScrollArea>
#include <QScrollBar>
#include "common/Constants.h"
#include "common/BurstAnimationHelper.h"

using namespace Darwin;

void ArrangementGridWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus();

    // プレイヘッドの移動（上部エリアクリック）
    if (event->position().y() < 5) {
        m_isDraggingPlayhead = true;
        double mx = event->position().x();
        qint64 newTick = qMax(0LL, static_cast<qint64>(mx / pixelsPerTick()));
        emit requestSeek(newTick);
        update();
        return;
    }

    // 右クリックで選択クリップを解除
    if (event->button() == Qt::RightButton) {
        m_selectedClipId = -1; // クリップの選択を解除
        update();
        return;
    }

    m_selectedClipId = -1;
    Clip* clickedClip = nullptr;

    int rowHeight = 100; // Assuming this is consistent

    if (m_project) {
        QList<Track*> visTracks = visibleTracks();
        for (int i = 0; i < visTracks.size(); ++i) {
            Track* track = visTracks.at(i);
            if (!track || track->isFolder()) continue;

            int yTrack = i * rowHeight;
            
            for (Clip* clip : track->clips()) {
                int x = static_cast<int>(clip->startTick() * pixelsPerTick());
                int w = static_cast<int>(clip->durationTicks() * pixelsPerTick());
                
                QRect clipRect(x, yTrack + 20, w, 60); 
                
                if (clipRect.contains(event->pos())) {
                    m_selectedClipId = clip->id();
                    clickedClip = clip;
                    m_lastMousePos = event->pos();
                    m_dragSourceTrackIndex = i;
                    m_dragCurrentTrackIndex = i;
                    
                    // Check if resizing (left or right edge)
                    if (event->pos().x() < clipRect.left() + 10) {
                        m_isResizingLeft = true;
                        m_isResizing = false;
                        m_isDragging = false;
                    } else if (event->pos().x() > clipRect.right() - 10) {
                        m_isResizing = true;
                        m_isResizingLeft = false;
                        m_isDragging = false;
                    } else {
                        m_isDragging = true;
                        m_isResizing = false;
                        m_isResizingLeft = false;
                        // 長押し分割タイマーを開始（クリップ中央部クリック時のみ）
                        m_longPressPos = event->pos();
                        m_longPressClipId = clip->id();
                        m_longPressTrackIdx = i;
                        m_longPressTimer.start();
                    }
                    break;
                }
            }
            if (m_selectedClipId != -1) break; // Found clip, break outer loop
        }
    }
    
    emit clipSelected(clickedClip);
    
    // クリップが見つからなかった場合、ラバーバンド範囲選択を開始
    if (!clickedClip && !m_isDraggingPlayhead) {
        m_isRubberBanding = true;
        m_rubberBandOrigin = event->pos();
        m_rubberBandRect = QRect();
        if (!(event->modifiers() & Qt::ControlModifier)) {
            m_selectedClipIds.clear();
        }
    }

    // 選択変更時のパルスアニメーション
    if (m_selectedClipId != -1 && m_selectedClipId != m_prevSelectedClipId) {
        startClipAnim(m_selectedClipId, ClipAnim::SelectPulse);
    }
    m_prevSelectedClipId = m_selectedClipId;
    
    update();
}

void ArrangementGridWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDraggingPlayhead) {
        if (!m_project) return;
        double mx = event->position().x();
        qint64 newTick = qMax(0LL, static_cast<qint64>(mx / pixelsPerTick()));
        if (newTick != m_playheadPosition) {
            emit requestSeek(newTick);
            ensurePlayheadVisible();
            update();
        }
        return;
    }

    // ラバーバンド範囲選択中
    if (m_isRubberBanding && m_project) {
        m_rubberBandRect = QRect(m_rubberBandOrigin, event->pos()).normalized();
        
        // ラバーバンド内のクリップを選択
        if (!(event->modifiers() & Qt::ControlModifier)) {
            m_selectedClipIds.clear();
        }
        int rowHeight = 100;
        QList<Track*> visTracks = visibleTracks();
        for (int i = 0; i < visTracks.size(); ++i) {
            Track* track = visTracks.at(i);
            if (!track || track->isFolder()) continue;
            int yTrack = i * rowHeight;
            for (Clip* clip : track->clips()) {
                int cx = static_cast<int>(clip->startTick() * pixelsPerTick());
                int cw = static_cast<int>(clip->durationTicks() * pixelsPerTick());
                QRect clipRect(cx, yTrack + 20, cw, 60);
                if (m_rubberBandRect.intersects(clipRect)) {
                    if (!m_selectedClipIds.contains(clip->id())) {
                        m_selectedClipIds.append(clip->id());
                    }
                }
            }
        }
        // 最後に選択したクリップを primary selection にする
        m_selectedClipId = m_selectedClipIds.isEmpty() ? -1 : m_selectedClipIds.last();
        update();
        return;
    }

    if (m_isDragging || m_isResizing || m_isResizingLeft) {
        // ドラッグ開始 → 長押しタイマーをキャンセル
        if (m_longPressTimer.isActive()) {
            QPoint delta = event->pos() - m_longPressPos;
            if (delta.manhattanLength() > 5) {
                m_longPressTimer.stop();
                m_longPressClipId = -1;
            }
        }

        if (!m_project) return;
        
        // Find the selected clip and its track
        Clip* selectedClip = nullptr;
        for (int i = 0; i < m_project->trackCount(); ++i) {
            for (Clip* clip : m_project->trackAt(i)->clips()) {
                if (clip->id() == m_selectedClipId) {
                    selectedClip = clip;
                    break;
                }
            }
        }
        
        if (selectedClip) {
            double dx = event->position().x() - m_lastMousePos.x();
            qint64 tickDelta = static_cast<qint64>(dx / pixelsPerTick());
            bool visualChanged = false;

            if (m_isDragging) {
                if (tickDelta != 0) {
                    selectedClip->setStartTick(qMax(0LL, selectedClip->startTick() + tickDelta));
                    visualChanged = true;
                }
                
                // トラック間移動: 現在のY座標からホバー先トラックを計算
                int rowHeight = 100;
                QList<Track*> visTracks = visibleTracks();
                int hoverTrack = qBound(0, event->pos().y() / rowHeight, visTracks.size() - 1);
                if (m_dragCurrentTrackIndex != hoverTrack) {
                    m_dragCurrentTrackIndex = hoverTrack;
                    visualChanged = true;
                }
            } else if (m_isResizingLeft) {
                // 左端リサイズ: startTickを移動し、durationを逆方向に調整
                qint64 newStart = selectedClip->startTick() + tickDelta;
                qint64 newDuration = selectedClip->durationTicks() - tickDelta;
                if (tickDelta != 0 && newStart >= 0 && newDuration >= TICKS_PER_BEAT) {
                    // 先にトリム/削除を行う（Note::setStartTick の qMax(0) クランプ前に判定）
                    QList<Note*> toRemove;
                    for (Note* note : selectedClip->notes()) {
                        qint64 noteEnd = note->startTick() + note->durationTicks();
                        if (noteEnd <= tickDelta) {
                            // ノート全体が削除範囲内 → 削除
                            toRemove.append(note);
                        } else if (note->startTick() < tickDelta) {
                            // ノートが部分的に削除範囲にかかる → 左側をトリム
                            qint64 overlap = tickDelta - note->startTick();
                            note->setStartTick(0);  // 新しいクリップ先頭に配置
                            note->setDurationTicks(note->durationTicks() - overlap);
                        } else {
                            // 範囲内 → 開始位置をシフト
                            note->setStartTick(note->startTick() - tickDelta);
                        }
                    }
                    for (Note* note : toRemove) {
                        selectedClip->removeNote(note);
                    }
                    selectedClip->setStartTick(newStart);
                    selectedClip->setDurationTicks(newDuration);
                    visualChanged = true;
                }
            } else if (m_isResizing) {
                qint64 newDuration = selectedClip->durationTicks() + tickDelta;
                if (tickDelta != 0 && newDuration >= TICKS_PER_BEAT) {
                    selectedClip->setDurationTicks(newDuration);
                    // 縮小でクリップ範囲外に出たノートをトリム/削除
                    QList<Note*> toRemove;
                    for (Note* note : selectedClip->notes()) {
                        if (note->startTick() >= newDuration) {
                            // 完全に範囲外 → 削除
                            toRemove.append(note);
                        } else if (note->startTick() + note->durationTicks() > newDuration) {
                            // 右端がはみ出し → durationをトリム
                            note->setDurationTicks(newDuration - note->startTick());
                        }
                    }
                    for (Note* note : toRemove) {
                        selectedClip->removeNote(note);
                    }
                    visualChanged = true;
                }
            }
            
            m_lastMousePos = event->pos();
            if (visualChanged) {
                update();
            }
        }
    } else {
        // カーソル形状をホバー状態に応じて更新
        if (m_project) {
            int rowHeight = 100;
            bool overEdge = false;
            QList<Track*> visTr = visibleTracks();
            for (int i = 0; i < visTr.size(); ++i) {
                Track* track = visTr.at(i);
                if (!track || track->isFolder()) continue;
                int yTrack = i * rowHeight;
                for (Clip* clip : track->clips()) {
                    int cx = static_cast<int>(clip->startTick() * pixelsPerTick());
                    int cw = static_cast<int>(clip->durationTicks() * pixelsPerTick());
                    QRect clipRect(cx, yTrack + 20, cw, 60);
                    if (clipRect.contains(event->pos())) {
                        if (event->pos().x() < clipRect.left() + 10 || event->pos().x() > clipRect.right() - 10) {
                            overEdge = true;
                        }
                        break;
                    }
                }
                if (overEdge) break;
            }
            setCursor(overEdge ? Qt::SizeHorCursor : Qt::ArrowCursor);
        }
    }
}

void ArrangementGridWidget::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event)

    // 長押しタイマーをキャンセル
    m_longPressTimer.stop();
    m_longPressClipId = -1;

    if (m_isDraggingPlayhead) {
        m_isDraggingPlayhead = false;
        update();
        return;
    }
    
    // ── 操作完了後のグリッドスナップ ──
    if ((m_isDragging || m_isResizing || m_isResizingLeft) && m_project && m_selectedClipId != -1) {
        Clip* snapClip = nullptr;
        for (int i = 0; i < m_project->trackCount(); ++i) {
            for (Clip* clip : m_project->trackAt(i)->clips()) {
                if (clip->id() == m_selectedClipId) { snapClip = clip; break; }
            }
            if (snapClip) break;
        }
        if (snapClip) {
            if (m_isDragging) {
                snapClip->setStartTick(snapTick(snapClip->startTick()));
            } else if (m_isResizingLeft) {
                qint64 oldEnd = snapClip->startTick() + snapClip->durationTicks();
                qint64 newStart = snapTick(snapClip->startTick());
                snapClip->setStartTick(newStart);
                snapClip->setDurationTicks(qMax((qint64)TICKS_PER_BEAT, oldEnd - newStart));
            } else if (m_isResizing) {
                qint64 rawEnd = snapClip->startTick() + snapClip->durationTicks();
                qint64 snappedEnd = snapTick(rawEnd);
                qint64 newDur = snappedEnd - snapClip->startTick();
                qint64 finalDur = qMax((qint64)TICKS_PER_BEAT, newDur);
                if (m_undoStack) {
                    m_undoStack->push(new ResizeClipCommand(snapClip, finalDur));
                } else {
                    snapClip->setDurationTicks(finalDur);
                }
            }
        }
    }

    // ── クリップのトラック間移動 ──
    if (m_isDragging && m_project && m_selectedClipId != -1 
        && m_dragSourceTrackIndex >= 0 && m_dragCurrentTrackIndex >= 0
        && m_dragSourceTrackIndex != m_dragCurrentTrackIndex) {
        
        QList<Track*> visTracks_ = visibleTracks();
        Track* srcTrack = (m_dragSourceTrackIndex < visTracks_.size()) ? visTracks_.at(m_dragSourceTrackIndex) : nullptr;
        Track* dstTrack = (m_dragCurrentTrackIndex < visTracks_.size()) ? visTracks_.at(m_dragCurrentTrackIndex) : nullptr;
        
        if (srcTrack && dstTrack) {
            // クリップを探す
            Clip* clipToMove = nullptr;
            for (Clip* clip : srcTrack->clips()) {
                if (clip->id() == m_selectedClipId) {
                    clipToMove = clip;
                    break;
                }
            }
            if (clipToMove) {
                if (m_undoStack) {
                    // トラック間移動コマンドは未実装なので注意
                    // 現在の UndoCommands.h には MoveClipCommand (startTickのみ) しかない
                    // とりあえず startTick の移動だけでもコマンド化する
                    m_undoStack->push(new MoveClipCommand(clipToMove, clipToMove->startTick()));
                }
                Clip* taken = srcTrack->takeClip(clipToMove);
                if (taken) {
                    dstTrack->insertClip(taken);
                    update();
                }
            }
        }
    } else if (m_isDragging && m_project && m_selectedClipId != -1) {
        // 同一トラック内の移動
        Clip* movedClip = nullptr;
        for (int i = 0; i < m_project->trackCount(); ++i) {
            for (Clip* clip : m_project->trackAt(i)->clips()) {
                if (clip->id() == m_selectedClipId) { movedClip = clip; break; }
            }
        }
        if (movedClip && m_undoStack) {
            m_undoStack->push(new MoveClipCommand(movedClip, movedClip->startTick()));
        }
    }
    
    m_isDragging = false;
    m_isResizing = false;
    m_isResizingLeft = false;
    m_dragSourceTrackIndex = -1;
    m_dragCurrentTrackIndex = -1;
    
    if (m_isRubberBanding) {
        m_isRubberBanding = false;
        m_rubberBandRect = QRect();
        update();
    }
}

void ArrangementGridWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_project) return;
    
    int rowHeight = 100;
    QList<Track*> visTracks_ = visibleTracks();
    int trackIndex = static_cast<int>(event->position().y()) / rowHeight;
    
    if (trackIndex >= 0 && trackIndex < visTracks_.size()) {
        Track* track = visTracks_.at(trackIndex);
        if (track->isFolder()) return; // フォルダ行ではクリップ操作しない
        int yTrack = trackIndex * rowHeight;
        
        // 既存クリップ上でダブルクリックした場合は削除（はじけアニメーション付き）
        for (Clip* clip : track->clips()) {
            int cx = static_cast<int>(clip->startTick() * pixelsPerTick());
            int cw = static_cast<int>(clip->durationTicks() * pixelsPerTick());
            QRect clipRect(cx, yTrack + 10, cw, rowHeight - 20);
            
            if (clipRect.contains(event->pos())) {
                if (clip->id() == m_selectedClipId) {
                    m_selectedClipId = -1;
                    emit clipSelected(nullptr);
                }
                startBurstAnim(QRectF(clipRect), track->color(), trackIndex);
                
                if (m_undoStack) {
                    m_undoStack->push(new RemoveClipCommand(track, clip));
                } else {
                    track->removeClip(clip);
                }
                update();
                return;
            }
        }
        
        // 空きスペースの場合は新規クリップを作成
        qint64 startTick = static_cast<qint64>(event->position().x() / pixelsPerTick());
        startTick = snapTick(startTick);
        
        qint64 durationTicks = TICKS_PER_BAR;
        
        if (m_undoStack) {
            m_undoStack->push(new AddClipCommand(track, startTick, durationTicks));
            // push後の最新のクリップを取得
            if (!track->clips().isEmpty()) {
                Clip* clip = track->clips().last();
                m_selectedClipId = clip->id();
                startClipAnim(clip->id(), ClipAnim::PopIn);
                emit clipSelected(clip);
            }
        } else {
            Clip* clip = track->addClip(startTick, durationTicks);
            m_selectedClipId = clip->id();
            startClipAnim(clip->id(), ClipAnim::PopIn);
            emit clipSelected(clip);
        }
        update();
    }
}

void ArrangementGridWidget::keyPressEvent(QKeyEvent *event)
{
    // Delete/Backspace: 選択クリップを削除（はじけアニメーション付き）
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (m_selectedClipId != -1 && m_project) {
            for (int i = 0; i < m_project->trackCount(); ++i) {
                Track* track = m_project->trackAt(i);
                int rowHeight = 100;
                int yTrack = i * rowHeight;
                for (Clip* clip : track->clips()) {
                    if (clip->id() == m_selectedClipId) {
                        int cx = static_cast<int>(clip->startTick() * pixelsPerTick());
                        int cw = static_cast<int>(clip->durationTicks() * pixelsPerTick());
                        QRect clipRect(cx, yTrack + 10, cw, rowHeight - 20);
                        startBurstAnim(QRectF(clipRect), track->color(), i);
                        track->removeClip(clip);
                        m_selectedClipId = -1;
                        update();
                        return;
                    }
                }
            }
        }
    }

    // Ctrl+C: 選択クリップをコピー
    if (event->key() == Qt::Key_C && (event->modifiers() & Qt::ControlModifier)) {
        if (m_selectedClipId != -1 && m_project) {
            for (int i = 0; i < m_project->trackCount(); ++i) {
                Track* track = m_project->trackAt(i);
                for (Clip* clip : track->clips()) {
                    if (clip->id() == m_selectedClipId) {
                        QJsonObject clipboardData;
                        clipboardData["type"] = "darwin_clip";
                        clipboardData["trackIndex"] = i;
                        clipboardData["clip"] = clip->toJson();

                        QJsonDocument doc(clipboardData);
                        QApplication::clipboard()->setText(doc.toJson(QJsonDocument::Compact));
                        return;
                    }
                }
            }
        }
    }

    // Ctrl+V: クリップをペースト
    if (event->key() == Qt::Key_V && (event->modifiers() & Qt::ControlModifier)) {
        if (!m_project) return;

        QString clipboardText = QApplication::clipboard()->text();
        if (clipboardText.isEmpty()) return;

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(clipboardText.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError) return;

        QJsonObject data = doc.object();
        if (data["type"].toString() != "darwin_clip") return;

        QJsonObject clipJson = data["clip"].toObject();
        qint64 durationTicks = static_cast<qint64>(clipJson["durationTicks"].toDouble(TICKS_PER_BAR));

        // マウスカーソル位置からペースト先を計算
        QPoint mousePos = mapFromGlobal(QCursor::pos());
        int rowHeight = 100;
        qint64 pasteStart = snapTick(static_cast<qint64>(mousePos.x() / pixelsPerTick()));

        // マウス位置のトラック行を特定
        QList<Track*> visTracks = visibleTracks();
        int targetRow = qMax(0, mousePos.y() / rowHeight);

        Track* track = nullptr;
        if (targetRow < visTracks.size()) {
            Track* candidate = visTracks.at(targetRow);
            // フォルダトラックは対象外
            if (candidate && !candidate->isFolder()) {
                track = candidate;
            }
        }

        // トラックが存在しない（範囲外 or フォルダ行）場合は新規作成
        if (!track) {
            int needed = targetRow - visTracks.size() + 1;
            for (int i = 0; i < needed; ++i) {
                track = m_project->addTrack();
            }
            if (!track) {
                track = m_project->addTrack();
            }
            updateDynamicSize();
        }

        Clip* newClip = track->addClip(pasteStart, durationTicks);

        // ノートをコピー
        QJsonArray notesArray = clipJson["notes"].toArray();
        for (const QJsonValue& val : notesArray) {
            QJsonObject noteJson = val.toObject();
            int pitch = noteJson["pitch"].toInt(60);
            qint64 noteSt = static_cast<qint64>(noteJson["startTick"].toDouble(0));
            qint64 noteDur = static_cast<qint64>(noteJson["durationTicks"].toDouble(480));
            int velocity = noteJson["velocity"].toInt(100);
            newClip->addNote(pitch, noteSt, noteDur, velocity);
        }

        m_selectedClipId = newClip->id();
        startClipAnim(newClip->id(), ClipAnim::PopIn);
        emit clipSelected(newClip);
        update();
    }

    // Ctrl+X: カット（コピー＋削除）
    if (event->key() == Qt::Key_X && (event->modifiers() & Qt::ControlModifier)) {
        if (m_selectedClipId != -1 && m_project) {
            for (int i = 0; i < m_project->trackCount(); ++i) {
                Track* track = m_project->trackAt(i);
                for (Clip* clip : track->clips()) {
                    if (clip->id() == m_selectedClipId) {
                        // コピー
                        QJsonObject clipboardData;
                        clipboardData["type"] = "darwin_clip";
                        clipboardData["trackIndex"] = i;
                        clipboardData["clip"] = clip->toJson();
                        QJsonDocument doc(clipboardData);
                        QApplication::clipboard()->setText(doc.toJson(QJsonDocument::Compact));

                        // 削除（はじけアニメーション付き）
                        int rowHeight = 100;
                        int yTrack = i * rowHeight;
                        int cx = static_cast<int>(clip->startTick() * pixelsPerTick());
                        int cw = static_cast<int>(clip->durationTicks() * pixelsPerTick());
                        QRect clipRect(cx, yTrack + 10, cw, rowHeight - 20);
                        startBurstAnim(QRectF(clipRect), track->color(), i);
                        if (m_undoStack) {
                            m_undoStack->push(new RemoveClipCommand(track, clip));
                        } else {
                            track->removeClip(clip);
                        }
                        m_selectedClipId = -1;
                        emit clipSelected(nullptr);
                        update();
                        return;
                    }
                }
            }
        }
    }

    // Gキーでスナップ切り替え
    if (event->key() == Qt::Key_G && m_project) {
        m_project->setGridSnapEnabled(!m_project->gridSnapEnabled());
        return;
    }

    // 左右矢印キーでプレイヘッドを移動
    if (event->key() == Qt::Key_Left) {
        if (m_project) {
            qint64 step = (m_project->gridSnapEnabled()) ? gridQuantize() : (TICKS_PER_BEAT / 4);
            qint64 newPos = qMax(0LL, m_project->playheadPosition() - step);
            emit requestSeek(newPos);
            ensurePlayheadVisible();
        }
        return;
    }
    if (event->key() == Qt::Key_Right) {
        if (m_project) {
            qint64 step = (m_project->gridSnapEnabled()) ? gridQuantize() : (TICKS_PER_BEAT / 4);
            qint64 newPos = m_project->playheadPosition() + step;
            emit requestSeek(newPos);
            ensurePlayheadVisible();
        }
        return;
    }

    QWidget::keyPressEvent(event);
}

void ArrangementGridWidget::wheelEvent(QWheelEvent *event)
{
    // Ctrl+スクロールでタイムライン拡大/縮小
    if (event->modifiers() & Qt::ControlModifier) {
        double delta = event->angleDelta().y();
        double factor = (delta > 0) ? 1.25 : 0.8; // ズームイン/アウト

        double newZoom = m_zoomLevel * factor;
        newZoom = qBound(0.25, newZoom, 4.0); // 0.25x 〜 4.0x の範囲

        if (!qFuzzyCompare(newZoom, m_zoomLevel)) {
            // ズーム中心をマウス位置に保つためのスクロール調整
            double mouseX = event->position().x();
            double oldTickAtMouse = mouseX / pixelsPerTick();

            m_zoomLevel = newZoom;
            updateDynamicSize();
            emit zoomChanged(m_zoomLevel);

            // マウス位置が同じtickを指すようスクロール位置を調整
            if (m_scrollArea) {
                double newMouseX = oldTickAtMouse * pixelsPerTick();
                QScrollBar* hBar = m_scrollArea->horizontalScrollBar();
                int scrollDelta = static_cast<int>(newMouseX - mouseX);
                hBar->setValue(hBar->value() + scrollDelta);
            }
        }
        event->accept();
    } else {
        // 通常のスクロールは親に任せる
        QWidget::wheelEvent(event);
    }
}
