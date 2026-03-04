/**
 * @file PianoRollGridWidget_Painting.cpp
 * @brief PianoRollGridWidget の描画処理（paintEvent）
 */
#include "PianoRollGridWidget.h"
#include "Clip.h"
#include "Note.h"
#include "Project.h"
#include "Track.h"
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
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    
    int widgetWidth = width();
    int gridHeight = NUM_ROWS * ROW_HEIGHT;
    double ppt = pixelsPerTick();
    double pixelsPerBar = BASE_PIXELS_PER_BAR * m_zoomLevel;
    double pixelsPerBeat = pixelsPerBar / BEATS_PER_BAR;
    
    // Background (白鍵行の色 = 鍵盤ウィジェットの白鍵と合わせる)
    p.fillRect(rect(), ThemeManager::instance().panelBackgroundColor());
    
    // アクティブクリップの範囲を計算
    double clipStartX = 0;
    double clipEndX = widgetWidth;
    if (m_activeClip) {
        clipStartX = m_activeClip->startTick() * ppt;
        clipEndX = m_activeClip->endTick() * ppt;
    }
    
    // 行（半音）を描画
    for(int i = 0; i < NUM_ROWS; ++i) {
        QRect rowRect(0, i * ROW_HEIGHT, widgetWidth, ROW_HEIGHT);
        int pitch = 127 - i;
        int pitchClass = pitch % 12;
        
        bool isBlackKey = (pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10);
        
        if(isBlackKey) {
            p.fillRect(rowRect, ThemeManager::instance().pianoBlackKeyColor());
        }
        
        p.setPen(ThemeManager::instance().gridLineTickColor()); // 横線は細めの区切り線
        p.drawLine(QPointF(0, (i + 1) * ROW_HEIGHT), QPointF(widgetWidth, (i + 1) * ROW_HEIGHT));
    }
    
    // ズームレベルに応じた縦線描画
    qint64 gridQ = gridQuantize();
    double pixelsPerGridUnit = gridQ * ppt;
    
    // グリッド線を描画
    if (pixelsPerGridUnit >= 4) { // 最低4px間隔で描画
        int numLines = static_cast<int>(widgetWidth / pixelsPerGridUnit) + 1;
        for (int idx = 0; idx <= numLines; ++idx) {
            double x = idx * pixelsPerGridUnit;
            qint64 tick = idx * gridQ;
            
            if (tick % TICKS_PER_BAR == 0) {
                // 小節線（最も濃い）
                p.setPen(ThemeManager::instance().gridLineColor());
            } else if (tick % TICKS_PER_BEAT == 0) {
                // 拍線
                p.setPen(ThemeManager::instance().gridLineSubColor());
            } else {
                // サブディビジョン（8分/16分/32分）
                p.setPen(ThemeManager::instance().gridLineSubBeatColor());
            }
            p.drawLine(QPointF(x, 0), QPointF(x, gridHeight));
        }
    } else {
        // ズームが小さすぎる場合は拍線のみ
        int numLines = static_cast<int>(widgetWidth / pixelsPerBeat) + 1;
        for (int idx = 0; idx <= numLines; ++idx) {
            double x = idx * pixelsPerBeat;
            qint64 tick = idx * TICKS_PER_BEAT;
            if (tick % TICKS_PER_BAR == 0) {
                p.setPen(ThemeManager::instance().gridLineColor());
            } else {
                p.setPen(ThemeManager::instance().gridLineSubColor());
            }
            p.drawLine(QPointF(x, 0), QPointF(x, gridHeight));
        }
    }
    
    // クリップ範囲外を暗くオーバーレイ（編集不可エリアを視覚化）
    if (m_activeClip) {
        QColor outOfRange(0, 0, 0, ThemeManager::instance().isDarkMode() ? 80 : 30);
        if (clipStartX > 0) {
            p.fillRect(0, 0, clipStartX, gridHeight, outOfRange);
        }
        if (clipEndX < widgetWidth) {
            p.fillRect(clipEndX, 0, widgetWidth - clipEndX, gridHeight, outOfRange);
        }
        p.setPen(QPen(QColor("#FF3366"), 1, Qt::DashLine));
        p.drawLine(clipStartX, 0, clipStartX, gridHeight);
        p.drawLine(clipEndX, 0, clipEndX, gridHeight);
    } else {
        // クリップ未選択時は全体を編集不可エリアと同じオーバーレイで暗くする
        p.fillRect(0, 0, widgetWidth, gridHeight, QColor(0, 0, 0, ThemeManager::instance().isDarkMode() ? 80 : 30));
    }
    
    // 表示中の全トラックのノートを描画（アクティブクリップ以外はゴーストノートとして）
    if (m_project) {
        for (Track* track : m_project->tracks()) {
            if (!track->isVisible()) continue; // 非表示トラックはスキップ
            
            QColor trackColor = track->color();
            QColor ghostColor = trackColor;
            ghostColor.setAlpha(60); // ゴーストノート用（薄い半透明でゴースト感を明確に）
            QColor ghostBorder = trackColor;
            ghostBorder.setAlpha(90);
            
            for (Clip* clip : track->clips()) {
                if (clip == m_activeClip) continue; // アクティブはスキップ（後で不透明に描画）
                
                for (Note* note : clip->notes()) {
                    double x = (clip->startTick() + note->startTick()) * pixelsPerTick();
                    double w = note->durationTicks() * pixelsPerTick();
                    int row = pitchToRow(note->pitch());
                    double y = row * ROW_HEIGHT;
                    
                    QRectF noteRect(x, y + 2, qMax(4.0, w), ROW_HEIGHT - 4);
                    
                    p.setRenderHint(QPainter::Antialiasing, true);
                    p.setPen(QPen(ghostBorder, 1));
                    p.setBrush(ghostColor);
                    p.drawRoundedRect(noteRect, 2, 2);
                    p.setRenderHint(QPainter::Antialiasing, false);
                }
            }
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
            double x = (note->startTick() + m_activeClip->startTick()) * pixelsPerTick();
            double w = note->durationTicks() * pixelsPerTick();
            
            int row = pitchToRow(note->pitch());
            double y = row * ROW_HEIGHT;
            
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
                p.setRenderHint(QPainter::Antialiasing, true);
                QColor glow = baseColor;
                glow.setAlphaF(glowIntensity * 0.5f);
                double expand = 3.0 * glowIntensity;
                QRectF glowRect = noteRect.adjusted(-expand, -expand, expand, expand);
                p.setPen(Qt::NoPen);
                p.setBrush(glow);
                p.drawRoundedRect(glowRect, 3, 3);
                p.setRenderHint(QPainter::Antialiasing, false);
            }

            // 選択中のノートは明るく表示
            QColor noteColor = isSelected ? baseColor.lighter(130) : baseColor;
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(noteColor.darker(120), 1));
            p.setBrush(noteColor);
            p.drawRoundedRect(noteRect, 2, 2);
            p.setRenderHint(QPainter::Antialiasing, false);
            
            p.restore();
        }

        // フェードアウト中の削除済みノートを描画
        for (Note* note : m_fadingNotes) {
            // すでに描画済み（clip->notes() にまだ含まれている）ならスキップ（連打対策）
            if (m_activeClip->notes().contains(note)) continue;

            Clip* parentClip = qobject_cast<Clip*>(note->parent());
            if (!parentClip) continue;
            Track* parentTrack = qobject_cast<Track*>(parentClip->parent());
            QColor color = parentTrack ? parentTrack->color() : QColor("#888888");

            double x = (note->startTick() + parentClip->startTick()) * pixelsPerTick();
            double w = note->durationTicks() * pixelsPerTick();
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
    
    // はじけ飛ぶ削除エフェクト
    drawBurstEffects(p);
    
    p.setPen(QPen(QColor("#FF3366"), 2));
    p.drawLine(QPointF(playheadX, 0), QPointF(playheadX, height()));
    
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
