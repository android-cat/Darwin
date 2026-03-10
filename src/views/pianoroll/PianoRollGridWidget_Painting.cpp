/**
 * @file PianoRollGridWidget_Painting.cpp
 * @brief PianoRollGridWidget の描画処理（paintEvent）
 */
#include "PianoRollGridWidget.h"
#include "Clip.h"
#include "Note.h"
#include "Project.h"
#include "Track.h"
#include <algorithm>
#include <QPaintEvent>
#include <QPainter>
#include <QPainter>
#include "common/Constants.h"
#include "common/BurstAnimationHelper.h"
#include "common/ThemeManager.h"

using namespace Darwin;
static const double BASE_PIXELS_PER_BAR = PIXELS_PER_BAR;

// ピッチ→行変換（ファイル間共有ヘルパー）
static int pitchToRow(int pitch) {
    return 127 - qBound(0, pitch, 127);
}

void PianoRollGridWidget::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    
    int widgetWidth = width();
    int gridHeight = NUM_ROWS * ROW_HEIGHT;
    double ppt = pixelsPerTick();
    // ビューポートカリング用の可視領域を取得
    const QRect visibleRect = event ? event->rect() : rect();
    const double visibleLeft = static_cast<double>(visibleRect.left());
    const double visibleRight = static_cast<double>(visibleRect.right());
    const int visibleTop = visibleRect.top();
    const int visibleBottom = visibleRect.bottom();
    double pixelsPerBar = BASE_PIXELS_PER_BAR * m_zoomLevel;
    double pixelsPerBeat = pixelsPerBar / BEATS_PER_BAR;
    
    // Background（可視領域のみ塗りつぶし）
    p.fillRect(visibleRect, ThemeManager::instance().panelBackgroundColor());
    
    // アクティブクリップの範囲を計算
    double clipStartX = 0;
    double clipEndX = widgetWidth;
    if (m_activeClip) {
        clipStartX = m_activeClip->startTick() * ppt;
        clipEndX = m_activeClip->endTick() * ppt;
    }
    
    // 行（半音）を描画 — ビューポート内の行のみ
    int firstRow = qMax(0, visibleTop / ROW_HEIGHT);
    int lastRow = qMin(NUM_ROWS - 1, visibleBottom / ROW_HEIGHT);
    for (int i = firstRow; i <= lastRow; ++i) {
        int pitch = 127 - i;
        int pitchClass = pitch % 12;
        
        bool isBlackKey = (pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10);
        
        if (isBlackKey) {
            QRect rowRect(visibleRect.left(), i * ROW_HEIGHT, visibleRect.width(), ROW_HEIGHT);
            p.fillRect(rowRect, ThemeManager::instance().pianoBlackKeyColor());
        }
        
        int lineY = (i + 1) * ROW_HEIGHT;
        p.setPen(ThemeManager::instance().gridLineTickColor());
        p.drawLine(QPointF(visibleLeft, lineY), QPointF(visibleRight, lineY));
    }
    
    // ズームレベルに応じた縦線描画 — ビューポート内のみ
    qint64 gridQ = gridQuantize();
    double pixelsPerGridUnit = gridQ * ppt;
    
    if (pixelsPerGridUnit >= 4) {
        // 可視領域の開始・終了インデックスを計算
        int startIdx = qMax(0, static_cast<int>(visibleLeft / pixelsPerGridUnit));
        int endIdx = static_cast<int>(visibleRight / pixelsPerGridUnit) + 1;
        for (int idx = startIdx; idx <= endIdx; ++idx) {
            double x = idx * pixelsPerGridUnit;
            qint64 tick = idx * gridQ;
            
            if (tick % TICKS_PER_BAR == 0) {
                p.setPen(ThemeManager::instance().gridLineColor());
            } else if (tick % TICKS_PER_BEAT == 0) {
                p.setPen(ThemeManager::instance().gridLineSubColor());
            } else {
                p.setPen(ThemeManager::instance().gridLineSubBeatColor());
            }
            p.drawLine(QPointF(x, visibleTop), QPointF(x, visibleBottom));
        }
    } else {
        int startIdx = qMax(0, static_cast<int>(visibleLeft / pixelsPerBeat));
        int endIdx = static_cast<int>(visibleRight / pixelsPerBeat) + 1;
        for (int idx = startIdx; idx <= endIdx; ++idx) {
            double x = idx * pixelsPerBeat;
            qint64 tick = idx * TICKS_PER_BEAT;
            if (tick % TICKS_PER_BAR == 0) {
                p.setPen(ThemeManager::instance().gridLineColor());
            } else {
                p.setPen(ThemeManager::instance().gridLineSubColor());
            }
            p.drawLine(QPointF(x, visibleTop), QPointF(x, visibleBottom));
        }
    }
    
    // クリップ範囲外を暗くオーバーレイ（編集不可エリアを視覚化）— ビューポート内のみ
    if (m_activeClip) {
        QColor outOfRange(0, 0, 0, ThemeManager::instance().isDarkMode() ? 80 : 30);
        if (clipStartX > visibleLeft) {
            p.fillRect(QRectF(visibleLeft, visibleTop, clipStartX - visibleLeft, visibleRect.height()), outOfRange);
        }
        if (clipEndX < visibleRight) {
            p.fillRect(QRectF(clipEndX, visibleTop, visibleRight - clipEndX, visibleRect.height()), outOfRange);
        }
        p.setPen(QPen(QColor("#FF3366"), 1, Qt::DashLine));
        if (clipStartX >= visibleLeft && clipStartX <= visibleRight)
            p.drawLine(clipStartX, visibleTop, clipStartX, visibleBottom);
        if (clipEndX >= visibleLeft && clipEndX <= visibleRight)
            p.drawLine(clipEndX, visibleTop, clipEndX, visibleBottom);
    } else {
        p.fillRect(visibleRect, QColor(0, 0, 0, ThemeManager::instance().isDarkMode() ? 80 : 30));
    }
    
    // ゴーストノートは事前に平坦化したキャッシュから、可視範囲近傍の要素だけ描画する。
    if (m_project) {
        if (m_ghostNoteCacheDirty) {
            rebuildGhostNoteCache();
        }

        const int activeClipId = m_activeClip ? m_activeClip->id() : -1;
        const qint64 visibleStartTick = qMax<qint64>(0, static_cast<qint64>(visibleLeft / ppt));
        const qint64 visibleEndTick = static_cast<qint64>(visibleRight / ppt) + 1;
        const qint64 searchStartTick = qMax<qint64>(0, visibleStartTick - m_maxGhostNoteDurationTicks);

        auto it = std::lower_bound(
            m_ghostNoteCache.begin(), m_ghostNoteCache.end(), searchStartTick,
            [](const GhostNoteCacheEntry& entry, qint64 tick) {
                return entry.startTick < tick;
            });

        for (; it != m_ghostNoteCache.end() && it->startTick <= visibleEndTick; ++it) {
            if (it->clipId == activeClipId) continue;

            const qint64 noteEndTick = it->startTick + it->durationTicks;
            if (noteEndTick < visibleStartTick) continue;

            int noteTop = it->row * ROW_HEIGHT;
            if (noteTop + ROW_HEIGHT < visibleTop || noteTop > visibleBottom) continue;

            double x = it->startTick * ppt;
            double w = it->durationTicks * ppt;
            QRectF noteRect(x, noteTop + 2, qMax(4.0, w), ROW_HEIGHT - 4);

            p.setPen(QPen(it->borderColor, 1));
            p.setBrush(it->fillColor);
            p.drawRoundedRect(noteRect, 2, 2);
        }
    }
    
    // アクティブクリップのノートを描画（不透明・完全表示）
    if (m_activeClip) {
        // アクティブクリップの親トラックの色を取得
        QColor baseColor("#FF3366"); // デフォルト
        Track* parentTrack = qobject_cast<Track*>(m_activeClip->parent());
        if (parentTrack) {
            baseColor = parentTrack->color();
        }
        
        for (Note* note : m_activeClip->notes()) {
            double x = (note->startTick() + m_activeClip->startTick()) * ppt;
            double w = note->durationTicks() * ppt;
            if (x + w < visibleLeft || x > visibleRight) continue;
            
            int row = pitchToRow(note->pitch());
            int noteTop = row * ROW_HEIGHT;
            if (noteTop + ROW_HEIGHT < visibleTop || noteTop > visibleBottom) continue;
            double y = noteTop;
            
            QRectF noteRect(x, y + 2, qMax(4.0, w), ROW_HEIGHT - 4);
            
            // アニメーション状態を取得
            float animScale = 1.0f;
            float animOpacity = 1.0f;
            float glowIntensity = 0.0f;

            if (m_noteAnims.contains(note)) {
                const NoteAnim& anim = m_noteAnims[note];
                if (anim.type == NoteAnim::PopIn) {
                    float t = anim.progress;
                    animScale = 0.6f + 0.4f * BurstAnimation::easeOutBack(t);
                    animOpacity = BurstAnimation::easeOutCubic(t);
                } else if (anim.type == NoteAnim::FadeOut) {
                    animOpacity = 1.0f - anim.progress;
                } else if (anim.type == NoteAnim::SelectGlow) {
                    float t = anim.progress;
                    glowIntensity = (1.0f - t) * 0.8f;
                    animScale = 1.0f + 0.05f * (1.0f - t);
                }
            }

            p.save();
            if (!qFuzzyCompare(animScale, 1.0f)) {
                QPointF center = noteRect.center();
                p.translate(center);
                p.scale(animScale, animScale);
                p.translate(-center);
            }
            p.setOpacity(animOpacity);

            // 選択中のノートは明るく表示
            bool isSelected = (note == m_selectedNote || m_selectedNotes.contains(note));
            if (isSelected && glowIntensity > 0.01f) {
                QColor glow = baseColor;
                glow.setAlphaF(glowIntensity * 0.5f);
                double expand = 3.0 * glowIntensity;
                QRectF glowRect = noteRect.adjusted(-expand, -expand, expand, expand);
                p.setPen(Qt::NoPen);
                p.setBrush(glow);
                p.drawRoundedRect(glowRect, 3, 3);
            }

            // 選択中のノートは明るく表示
            QColor noteColor = isSelected ? baseColor.lighter(130) : baseColor;
            p.setPen(QPen(noteColor.darker(120), 1));
            p.setBrush(noteColor);
            p.drawRoundedRect(noteRect, 2, 2);
            
            p.restore();
        }

        // フェードアウト中の削除済みノートを描画
        for (const QPointer<Note>& notePtr : m_fadingNotes) {
            Note* note = notePtr.data();
            if (!note) continue;
            // すでに描画済み（clip->notes() にまだ含まれている）ならスキップ（連打対策）
            if (m_activeClip->notes().contains(note)) continue;

            Clip* parentClip = qobject_cast<Clip*>(note->parent());
            if (!parentClip) continue;
            Track* parentTrack = qobject_cast<Track*>(parentClip->parent());
            QColor color = parentTrack ? parentTrack->color() : QColor("#888888");

            double x = (note->startTick() + parentClip->startTick()) * pixelsPerTick();
            double w = note->durationTicks() * pixelsPerTick();
            if (x + w < visibleLeft || x > visibleRight) continue;
            int row = pitchToRow(note->pitch());
            double y = row * ROW_HEIGHT;
            QRectF noteRect(x, y + 2, qMax(4.0, w), ROW_HEIGHT - 4);

            float opacity = 1.0f;
            if (m_noteAnims.contains(note)) {
                opacity = 1.0f - m_noteAnims[note].progress;
            }

            p.save();
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setOpacity(opacity);
            p.setPen(QPen(color.darker(120), 1));
            p.setBrush(color);
            p.drawRoundedRect(noteRect, 2, 2);
            p.restore();
        }
    } else if (!m_project) {
        // プロジェクトもクリップもない場合のみテキスト表示
        p.setPen(ThemeManager::instance().secondaryTextColor());
        p.setFont(QFont("Segoe UI", 12));
        p.drawText(rect(), Qt::AlignCenter, "Select a clip in the Arrangement View to edit notes");
    }
    
    // Playhead
    double playheadX = m_playheadPosition * pixelsPerTick();
    p.setRenderHint(QPainter::Antialiasing, true);

    // 再生中の「光の軌跡（モーションブラー）」演出
    if (m_trailOpacity > 0.001f) {
        double trailLen = 80.0;
        QLinearGradient trailGradient(playheadX, 0, playheadX - trailLen, 0);
        QColor trailColor = QColor("#FF3366");
        trailColor.setAlpha(static_cast<int>(100 * m_trailOpacity)); 
        trailGradient.setColorAt(0, trailColor);
        trailGradient.setColorAt(1, Qt::transparent);

        p.fillRect(QRectF(playheadX - trailLen, 0, trailLen, height()), trailGradient);
    }

    // メインの再生ヘッド線
    p.setPen(QPen(QColor("#FF3366"), 2));
    p.drawLine(QPointF(playheadX, 0), QPointF(playheadX, height()));
    p.setRenderHint(QPainter::Antialiasing, false);
    
    // 範囲選択ラバーバンド描画
    if (m_isRubberBanding && m_rubberBandRect.isValid()) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(QColor(59, 130, 246, 200), 1, Qt::DashLine));
        p.setBrush(QColor(59, 130, 246, 40));
        p.drawRect(m_rubberBandRect);
        p.restore();
    }
}
