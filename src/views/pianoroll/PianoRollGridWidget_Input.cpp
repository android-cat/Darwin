/**
 * @file PianoRollGridWidget_Input.cpp
 * @brief PianoRollGridWidget のマウス・キー・ホイール入力処理
 */
#include "PianoRollGridWidget.h"
#include "models/Clip.h"
#include "models/Note.h"
#include "models/Track.h"
#include "Project.h"
#include "commands/UndoCommands.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QToolTip>
#include "common/Constants.h"
#include "common/BurstAnimationHelper.h"
#include "common/ChordDetector.h"

using namespace Darwin;
static const double BASE_PIXELS_PER_BAR = PIXELS_PER_BAR;

// ピッチ←→行変換（ファイル間共有ヘルパー）
static int pitchToRow(int pitch) {
    return 127 - qBound(0, pitch, 127);
}

static int rowToPitch(int row) {
    return 127 - qBound(0, row, 127);
}

void PianoRollGridWidget::mousePressEvent(QMouseEvent *event)
{
    // プレイヘッド位置のドラッグ判定
    int playheadX = static_cast<int>(m_playheadPosition * pixelsPerTick());
    if (qAbs(event->pos().x() - playheadX) <= 15) {
        m_isDraggingPlayhead = true;
        m_selectedNote = nullptr;
        m_selectedNotes.clear();
        update();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        // 長押しタイマーを開始
        m_pressPos = event->pos();
        m_lastMousePos = event->pos();
        m_longPressActive = false;
        m_longPressTimer.start();
    }
    
    if (!m_activeClip) return;
    
    Note* clickedNote = nullptr;
    
    const auto& notes = m_activeClip->notes();
    for (int i = notes.size() - 1; i >= 0; --i) {
        Note* note = notes[i];
        
        int x = static_cast<int>((note->startTick() + m_activeClip->startTick()) * pixelsPerTick());
        int w = static_cast<int>(note->durationTicks() * pixelsPerTick());
        int y = pitchToRow(note->pitch()) * ROW_HEIGHT;
        
        QRect noteRect(x, y, qMax(4, w), ROW_HEIGHT);
        
        if (noteRect.contains(event->pos())) {
            clickedNote = note;
            m_lastMousePos = event->pos();
            m_longPressTimer.stop(); // ノートクリックは長押し判定しない
            
            // 左端・右端・ドラッグの判定
            if (event->pos().x() < noteRect.left() + 8) {
                m_isResizingLeft = true;
                m_isResizing = false;
                m_isDragging = false;
            } else if (event->pos().x() > noteRect.right() - 8) {
                m_isResizing = true;
                m_isResizingLeft = false;
                m_isDragging = false;
            } else {
                m_isResizing = false;
                m_isResizingLeft = false;
                m_isDragging = true;
            }
            break;
        }
    }
    
    if (clickedNote) {
        // Ctrl+クリックで複数選択の追加/解除
        if (event->modifiers() & Qt::ControlModifier) {
            if (m_selectedNotes.contains(clickedNote)) {
                m_selectedNotes.removeOne(clickedNote);
                if (m_selectedNote == clickedNote) {
                    m_selectedNote = m_selectedNotes.isEmpty() ? nullptr : m_selectedNotes.last();
                }
            } else {
                m_selectedNotes.append(clickedNote);
                m_selectedNote = clickedNote;
            }
        } else {
            // 複数選択中のノートをクリックした場合は選択を維持してドラッグ
            if (!m_selectedNotes.contains(clickedNote)) {
                m_selectedNotes.clear();
                m_selectedNotes.append(clickedNote);
            }
            m_selectedNote = clickedNote;
        }
    } else {
        // 空白エリアをクリック → 範囲選択開始
        if (!(event->modifiers() & Qt::ControlModifier)) {
            m_selectedNote = nullptr;
            m_selectedNotes.clear();
        }
        m_isRubberBanding = true;
        m_rubberBandOrigin = event->pos();
        m_rubberBandRect = QRect();
        m_isDragging = false;
        m_isResizing = false;
        m_isResizingLeft = false;
        m_longPressTimer.stop();  // ラバーバンド中はズームモード抑止
    }
    
    // 選択変更時のグローアニメーション
    if (m_selectedNote && m_selectedNote != m_prevSelectedNote) {
        startNoteAnim(m_selectedNote, NoteAnim::SelectGlow);
    }
    m_prevSelectedNote = m_selectedNote;
    
    update();
}

void PianoRollGridWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDraggingPlayhead) {
        if (!m_project) return;
        double mx = event->position().x();
        qint64 newTick = qMax(0LL, static_cast<qint64>(mx / pixelsPerTick()));
        emit requestSeek(newTick);
        ensurePlayheadVisible();
        update();
        return;
    }

    // 長押しドラッグズーム
    if (m_isZooming) {
        int dx = event->pos().x() - m_lastMousePos.x();
        if (qAbs(dx) > 2) {
            double factor = 1.0 + (dx * 0.005);
            double newZoom = qBound(0.25, m_zoomLevel * factor, 16.0);
            
            if (!qFuzzyCompare(newZoom, m_zoomLevel)) {
                m_zoomLevel = newZoom;
                updateDynamicSize();
            }
            m_lastMousePos = event->pos();
        }
        return;
    }
    
    // 長押しタイマーが動作中に大きく動いたらタイマーをキャンセル（通常のドラッグと判定）
    if (m_longPressTimer.isActive()) {
        QPoint delta = event->pos() - m_pressPos;
        if (delta.manhattanLength() > 5) {
            m_longPressTimer.stop();
        }
    }
    
    // ラバーバンド描画中
    if (m_isRubberBanding && m_activeClip) {
        m_rubberBandRect = QRect(m_rubberBandOrigin, event->pos()).normalized();
        
        // ラバーバンド内のノートを選択
        if (!(event->modifiers() & Qt::ControlModifier)) {
            m_selectedNotes.clear();
        }
        for (Note* note : m_activeClip->notes()) {
            int nx = static_cast<int>((note->startTick() + m_activeClip->startTick()) * pixelsPerTick());
            int nw = static_cast<int>(note->durationTicks() * pixelsPerTick());
            int ny = pitchToRow(note->pitch()) * ROW_HEIGHT;
            QRect noteRect(nx, ny, qMax(4, nw), ROW_HEIGHT);
            
            if (m_rubberBandRect.intersects(noteRect)) {
                if (!m_selectedNotes.contains(note)) {
                    m_selectedNotes.append(note);
                }
            }
        }
        m_selectedNote = m_selectedNotes.isEmpty() ? nullptr : m_selectedNotes.last();
        update();
        return;
    }

    if (!m_activeClip || !m_selectedNote) {
        bool overLeftEdge = false;
        bool overRightEdge = false;
        Note* hoveredNote = nullptr;
        if (m_activeClip) {
            for (Note* note : m_activeClip->notes()) {
                int x = static_cast<int>((note->startTick() + m_activeClip->startTick()) * pixelsPerTick());
                int w = static_cast<int>(note->durationTicks() * pixelsPerTick());
                int y = pitchToRow(note->pitch()) * ROW_HEIGHT;
                QRect noteRect(x, y, qMax(4, w), ROW_HEIGHT);
                
                if (noteRect.contains(event->pos())) {
                    hoveredNote = note;
                    if (event->pos().x() < noteRect.left() + 8) {
                        overLeftEdge = true;
                    } else if (event->pos().x() > noteRect.right() - 8) {
                        overRightEdge = true;
                    }
                    break;
                }
            }
        }
        setCursor((overLeftEdge || overRightEdge) ? Qt::SizeHorCursor : Qt::ArrowCursor);

        // ===== ノートホバー時コード名ツールチップ =====
        if (hoveredNote && m_activeClip) {
            // ホバー中のノートと時間的に重なる全ノートからコードを検出
            qint64 hStart = m_activeClip->startTick() + hoveredNote->startTick();
            qint64 hEnd   = hStart + hoveredNote->durationTicks();

            QSet<int> pitchClasses;
            int minPitch = 128;

            // アクティブクリップのノート
            for (Note* note : m_activeClip->notes()) {
                qint64 absStart = m_activeClip->startTick() + note->startTick();
                qint64 absEnd   = absStart + note->durationTicks();
                if (absStart < hEnd && absEnd > hStart) {
                    pitchClasses.insert(note->pitch() % 12);
                    if (note->pitch() < minPitch) minPitch = note->pitch();
                }
            }
            // 他トラックのゴーストノートも考慮
            if (m_project) {
                for (Track* track : m_project->tracks()) {
                    if (!track->isVisible()) continue;
                    for (Clip* clip : track->clips()) {
                        if (clip == m_activeClip) continue;
                        for (Note* note : clip->notes()) {
                            qint64 absStart = clip->startTick() + note->startTick();
                            qint64 absEnd   = absStart + note->durationTicks();
                            if (absStart < hEnd && absEnd > hStart) {
                                pitchClasses.insert(note->pitch() % 12);
                                if (note->pitch() < minPitch) minPitch = note->pitch();
                            }
                        }
                    }
                }
            }

            int bassPitchClass = (minPitch < 128) ? (minPitch % 12) : -1;
            QString chordName = ChordDetector::detect(pitchClasses, bassPitchClass);
            if (!chordName.isEmpty()) {
                QToolTip::showText(event->globalPosition().toPoint(), chordName, this);
            } else {
                QToolTip::hideText();
            }
        } else {
            QToolTip::hideText();
        }

        return;
    }
    
    int dx = event->pos().x() - m_lastMousePos.x();
    int dy = event->pos().y() - m_lastMousePos.y();
    
    if (m_isDragging) {
        // Move all selected notes
        qint64 dticks = static_cast<qint64>(dx / pixelsPerTick());
        int rowsMoved = 0;
        if (qAbs(dy) >= ROW_HEIGHT) {
            rowsMoved = dy / ROW_HEIGHT;
        }
        
        if (dticks != 0) {
            for (Note* note : m_selectedNotes) {
                note->setStartTick(qMax(0LL, note->startTick() + dticks));
            }
            m_lastMousePos.setX(event->pos().x());
        }
        
        if (rowsMoved != 0) {
            for (Note* note : m_selectedNotes) {
                int newPitch = note->pitch() - rowsMoved;
                newPitch = qBound(0, newPitch, 127);
                note->setPitch(newPitch);
            }
            m_lastMousePos.setY(event->pos().y());
        }
    } else if (m_isResizingLeft) {
        // 左端リサイズ（複数選択対応）
        qint64 dticks = static_cast<qint64>(dx / pixelsPerTick());
        if (dticks != 0) {
            for (Note* note : m_selectedNotes) {
                qint64 newStart = note->startTick() + dticks;
                qint64 newDuration = note->durationTicks() - dticks;
                if (newStart >= 0 && newDuration >= static_cast<qint64>(TICKS_PER_BEAT / 4)) {
                    note->setStartTick(newStart);
                    note->setDurationTicks(newDuration);
                }
            }
            m_lastMousePos.setX(event->pos().x());
        }
    } else if (m_isResizing) {
        // 右端リサイズ（複数選択対応）
        qint64 dticks = static_cast<qint64>(dx / pixelsPerTick());
        if (dticks != 0) {
            for (Note* note : m_selectedNotes) {
                qint64 newDuration = note->durationTicks() + dticks;
                note->setDurationTicks(qMax(static_cast<qint64>(TICKS_PER_BEAT / 4), newDuration));
            }
            m_lastMousePos.setX(event->pos().x());
        }
    }
}

void PianoRollGridWidget::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event)

    if (m_isDraggingPlayhead) {
        m_isDraggingPlayhead = false;
        update();
        return;
    }

    m_longPressTimer.stop();
    
    if (m_isZooming) {
        m_isZooming = false;
        m_longPressActive = false;
        setCursor(Qt::ArrowCursor);
        return;
    }
    
    if (m_isRubberBanding) {
        m_isRubberBanding = false;
        m_rubberBandRect = QRect();
        update();
    }
    
    // ── 操作完了後のグリッドスナップ ──
    if ((m_isDragging || m_isResizing || m_isResizingLeft) && m_activeClip && !m_selectedNotes.isEmpty()) {
        for (Note* note : m_selectedNotes) {
            if (m_isDragging) {
                qint64 finalTick = snapTick(note->startTick());
                int finalPitch = note->pitch();
                if (m_undoStack) {
                    m_undoStack->push(new MoveNoteCommand(note, finalPitch, finalTick));
                } else {
                    note->setStartTick(finalTick);
                }
            } else if (m_isResizingLeft) {
                qint64 oldEnd = note->startTick() + note->durationTicks();
                qint64 newStart = snapTick(note->startTick());
                note->setStartTick(newStart);
                note->setDurationTicks(qMax(static_cast<qint64>(TICKS_PER_BEAT / 4), oldEnd - newStart));
            } else if (m_isResizing) {
                qint64 oldEnd = note->startTick() + note->durationTicks();
                qint64 snappedEnd = snapTick(oldEnd);
                qint64 newDur = qMax((qint64)1, snappedEnd - note->startTick());
                if (m_undoStack) {
                    m_undoStack->push(new ResizeNoteCommand(note, newDur));
                } else {
                    note->setDurationTicks(newDur);
                }
            }
        }
        update();
    }

    m_longPressActive = false;
    m_isDragging = false;
    m_isResizing = false;
    m_isResizingLeft = false;
}

void PianoRollGridWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_activeClip) return;
    
    // 既存ノート上でダブルクリックした場合は削除（はじけアニメーション付き）
    const auto& notes = m_activeClip->notes();
    for (int i = notes.size() - 1; i >= 0; --i) {
        Note* note = notes[i];
        int x = static_cast<int>((note->startTick() + m_activeClip->startTick()) * pixelsPerTick());
        int w = static_cast<int>(note->durationTicks() * pixelsPerTick());
        int y = pitchToRow(note->pitch()) * ROW_HEIGHT;
        QRect noteRect(x, y, qMax(4, w), ROW_HEIGHT);
        
        if (noteRect.contains(event->pos())) {
            QColor baseColor("#FF3366");
            Track* parentTrack = qobject_cast<Track*>(m_activeClip->parent());
            if (parentTrack) baseColor = parentTrack->color();
            startBurstAnim(QRectF(noteRect), baseColor);
            markNoteForBurstRemoval(note);
            
            if (m_undoStack) {
                m_undoStack->push(new RemoveNoteCommand(m_activeClip, note));
            } else {
                m_activeClip->removeNote(note);
            }
            m_selectedNote = nullptr;
            m_selectedNotes.removeOne(note);
            update();
            return;
        }
    }
    
    // 空きスペースの場合は新規ノートを作成
    int row = event->pos().y() / ROW_HEIGHT;
    int pitch = rowToPitch(row);
    pitch = qBound(0, pitch, 127);
    
    qint64 quantize = gridQuantize();
    qint64 startTickAbs = event->pos().x() / pixelsPerTick();
    startTickAbs = (startTickAbs / quantize) * quantize; // グリッドにスナップ
    
    // クリップ範囲外への配置を禁止
    if (startTickAbs < m_activeClip->startTick() || startTickAbs >= m_activeClip->endTick()) {
        return;
    }
    
    qint64 startTickRel = startTickAbs - m_activeClip->startTick();
    
    // ノートの長さは現在のグリッド量子化単位
    qint64 maxDuration = m_activeClip->durationTicks() - startTickRel;
    qint64 duration = qMin(quantize, maxDuration);
    if (duration <= 0) return;
    
    if (m_undoStack) {
        m_undoStack->push(new AddNoteCommand(m_activeClip, pitch, startTickRel, duration, 100));
        if (!m_activeClip->notes().isEmpty()) {
            m_selectedNote = m_activeClip->notes().last();
            m_selectedNotes.clear();
            m_selectedNotes.append(m_selectedNote);
            startNoteAnim(m_selectedNote, NoteAnim::PopIn);
        }
    } else {
        m_selectedNote = m_activeClip->addNote(pitch, startTickRel, duration, 100);
        m_selectedNotes.clear();
        m_selectedNotes.append(m_selectedNote);
        startNoteAnim(m_selectedNote, NoteAnim::PopIn);
    }
    update();
}

void PianoRollGridWidget::keyPressEvent(QKeyEvent *event)
{
    if (!m_activeClip) return;
    
    // Delete/Backspace: 選択ノートを一括削除（はじけアニメーション付き）
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && !m_selectedNotes.isEmpty()) {
        QColor baseColor("#FF3366");
        Track* parentTrack = qobject_cast<Track*>(m_activeClip->parent());
        if (parentTrack) baseColor = parentTrack->color();
        
        for (Note* note : m_selectedNotes) {
            int x = static_cast<int>((note->startTick() + m_activeClip->startTick()) * pixelsPerTick());
            int w = static_cast<int>(note->durationTicks() * pixelsPerTick());
            int y = pitchToRow(note->pitch()) * ROW_HEIGHT;
            QRect noteRect(x, y + 2, qMax(4, w), ROW_HEIGHT - 4);
            startBurstAnim(QRectF(noteRect), baseColor);
            markNoteForBurstRemoval(note);
            m_activeClip->removeNote(note);
        }
        m_selectedNote = nullptr;
        m_selectedNotes.clear();
        update();
        return;
    }

    // Ctrl+A: 全ノート選択
    if (event->key() == Qt::Key_A && (event->modifiers() & Qt::ControlModifier)) {
        m_selectedNotes.clear();
        for (Note* note : m_activeClip->notes()) {
            m_selectedNotes.append(note);
        }
        m_selectedNote = m_selectedNotes.isEmpty() ? nullptr : m_selectedNotes.last();
        update();
        return;
    }

    // Ctrl+C: 選択ノートをコピー
    if (event->key() == Qt::Key_C && (event->modifiers() & Qt::ControlModifier)) {
        if (!m_selectedNotes.isEmpty()) {
            QJsonObject clipboardData;
            clipboardData["type"] = "darwin_notes";

            QJsonArray notesArray;
            if (event->modifiers() & Qt::ShiftModifier) {
                // Ctrl+Shift+C: 全ノートをコピー
                for (const Note* note : m_activeClip->notes()) {
                    notesArray.append(note->toJson());
                }
            } else {
                // Ctrl+C: 選択ノートをコピー
                for (const Note* note : m_selectedNotes) {
                    notesArray.append(note->toJson());
                }
            }
            clipboardData["notes"] = notesArray;

            QJsonDocument doc(clipboardData);
            QApplication::clipboard()->setText(doc.toJson(QJsonDocument::Compact));
        }
        return;
    }

    // Ctrl+V: ペースト
    if (event->key() == Qt::Key_V && (event->modifiers() & Qt::ControlModifier)) {
        QString clipboardText = QApplication::clipboard()->text();
        if (clipboardText.isEmpty()) return;

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(clipboardText.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError) return;

        QJsonObject data = doc.object();
        if (data["type"].toString() != "darwin_notes") return;

        QJsonArray notesArray = data["notes"].toArray();
        if (notesArray.isEmpty()) return;

        // ペースト先: 最も早いノートのオフセットを基準に、現在のクリップ先頭にペースト
        qint64 minStart = std::numeric_limits<qint64>::max();
        for (const QJsonValue& val : notesArray) {
            qint64 st = static_cast<qint64>(val.toObject()["startTick"].toDouble());
            minStart = qMin(minStart, st);
        }

        Note* lastPasted = nullptr;
        for (const QJsonValue& val : notesArray) {
            QJsonObject noteJson = val.toObject();
            int pitch = noteJson["pitch"].toInt(60);
            qint64 startTick = static_cast<qint64>(noteJson["startTick"].toDouble()) - minStart;
            qint64 duration = static_cast<qint64>(noteJson["durationTicks"].toDouble(480));
            int velocity = noteJson["velocity"].toInt(100);

            // クリップ範囲内に収まるか確認
            if (startTick >= 0 && startTick < m_activeClip->durationTicks()) {
                qint64 maxDur = m_activeClip->durationTicks() - startTick;
                duration = qMin(duration, maxDur);
                lastPasted = m_activeClip->addNote(pitch, startTick, duration, velocity);
                if (lastPasted) {
                    startNoteAnim(lastPasted, NoteAnim::PopIn);
                }
            }
        }

        if (lastPasted) {
            m_selectedNote = lastPasted;
        }
        update();
    }

    // Ctrl+X: カット（コピー＋削除）
    if (event->key() == Qt::Key_X && (event->modifiers() & Qt::ControlModifier)) {
        if (!m_selectedNotes.isEmpty()) {
            // まずコピー
            QJsonObject clipboardData;
            clipboardData["type"] = "darwin_notes";
            QJsonArray notesArray;
            for (const Note* note : m_selectedNotes) {
                notesArray.append(note->toJson());
            }
            clipboardData["notes"] = notesArray;
            QJsonDocument doc(clipboardData);
            QApplication::clipboard()->setText(doc.toJson(QJsonDocument::Compact));

            // 一括削除（はじけアニメーション付き）
            QColor baseColor("#FF3366");
            Track* parentTrack = qobject_cast<Track*>(m_activeClip->parent());
            if (parentTrack) baseColor = parentTrack->color();
            
            for (Note* note : m_selectedNotes) {
                int nx = static_cast<int>((note->startTick() + m_activeClip->startTick()) * pixelsPerTick());
                int nw = static_cast<int>(note->durationTicks() * pixelsPerTick());
                int ny = pitchToRow(note->pitch()) * ROW_HEIGHT;
                QRect noteRect(nx, ny + 2, qMax(4, nw), ROW_HEIGHT - 4);
                startBurstAnim(QRectF(noteRect), baseColor);
                markNoteForBurstRemoval(note);
                if (m_undoStack) {
                    m_undoStack->push(new RemoveNoteCommand(m_activeClip, note));
                } else {
                    m_activeClip->removeNote(note);
                }
            }
            m_selectedNote = nullptr;
            m_selectedNotes.clear();
            update();
        }
        return;
    }

    // Gキーでスナップ切り替え
    if (event->key() == Qt::Key_G && m_project) {
        m_project->setGridSnapEnabled(!m_project->gridSnapEnabled());
        return;
    }

    // 左右矢印キーでプレイヘッドを移動
    if (event->key() == Qt::Key_Left) {
        if (m_project) {
            qint64 step = gridQuantize();
            qint64 newPos = qMax(0LL, m_project->playheadPosition() - step);
            emit requestSeek(newPos);
            ensurePlayheadVisible();
            update();
        }
        return;
    }
    if (event->key() == Qt::Key_Right) {
        if (m_project) {
            qint64 step = gridQuantize();
            qint64 newPos = m_project->playheadPosition() + step;
            emit requestSeek(newPos);
            ensurePlayheadVisible();
            update();
        }
        return;
    }

    QWidget::keyPressEvent(event);
}

void PianoRollGridWidget::wheelEvent(QWheelEvent *event)
{
    // Ctrl+スクロールでタイムライン拡大/縮小
    if (event->modifiers() & Qt::ControlModifier) {
        double delta = event->angleDelta().y();
        double factor = (delta > 0) ? 1.25 : 0.8; // ズームイン/アウト
        
        double newZoom = m_zoomLevel * factor;
        newZoom = qBound(0.25, newZoom, 16.0); // 0.25x 〜 16x の範囲
        
        if (!qFuzzyCompare(newZoom, m_zoomLevel)) {
            m_zoomLevel = newZoom;
            updateDynamicSize();
        }
        event->accept();
    } else {
        // 通常のスクロールは親に任せる
        QWidget::wheelEvent(event);
    }
}
