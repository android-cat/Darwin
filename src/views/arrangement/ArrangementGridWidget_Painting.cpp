/**
 * @file ArrangementGridWidget_Painting.cpp
 * @brief ArrangementGridWidget の描画処理（paintEvent, drawClips, drawFolderSummary, drawAudioWaveform）
 */
#include "ArrangementGridWidget.h"
#include "models/Project.h"
#include "models/Track.h"
#include "models/Clip.h"
#include "models/Note.h"
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPath>
#include "common/Constants.h"
#include "common/BurstAnimationHelper.h"
#include "common/ThemeManager.h"

using namespace Darwin;

void ArrangementGridWidget::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    int rowHeight = 100;
    double ppBar = pixelsPerBar();
    double ppBeat = ppBar / BEATS_PER_BAR;
    
    int numRows = (m_project) ? visibleTracks().size() : 0;
    if (numRows == 0) numRows = 4;

    int widgetWidth = width();
    int widgetHeight = numRows * rowHeight;
    if (widgetHeight < height()) widgetHeight = height();
    
    // ビューポートカリング用の可視領域を取得
    const QRect visibleRect = event ? event->rect() : rect();
    const double visibleLeft = static_cast<double>(visibleRect.left());
    const double visibleRight = static_cast<double>(visibleRect.right());
    
    // Background（可視領域のみ塗りつぶし）
    p.fillRect(visibleRect, ThemeManager::instance().backgroundColor());
    
    // Grid Lines (Vertical) — ビューポート内のみ描画
    const int MIN_SUBDIV_PX = 12;
    
    int subdivPerBeat = 1;
    if (ppBeat / 2 >= MIN_SUBDIV_PX)  subdivPerBeat = 2;
    if (ppBeat / 4 >= MIN_SUBDIV_PX)  subdivPerBeat = 4;
    if (ppBeat / 8 >= MIN_SUBDIV_PX)  subdivPerBeat = 8;
    
    int subdivsPerBar = BEATS_PER_BAR * subdivPerBeat;
    double subdivWidth = ppBar / subdivsPerBar;
    
    // 可視範囲の開始・終了インデックスを計算
    int startIdx = qMax(0, static_cast<int>(visibleLeft / subdivWidth));
    int endIdx = static_cast<int>(visibleRight / subdivWidth) + 1;
    for (int idx = startIdx; idx <= endIdx; ++idx) {
        double x = idx * ppBar / subdivsPerBar;
        if (x >= widgetWidth) break;
        
        if (idx % subdivsPerBar == 0) {
            p.setPen(ThemeManager::instance().gridLineColor());
        } else if (idx % subdivPerBeat == 0) {
            p.setPen(ThemeManager::instance().gridLineSubColor());
        } else if (subdivPerBeat >= 4 && idx % (subdivPerBeat / 2) == 0) {
            p.setPen(ThemeManager::instance().gridLineSubBeatColor());
        } else {
            p.setPen(ThemeManager::instance().gridLineTickColor());
        }
        p.drawLine(QPointF(x, visibleRect.top()), QPointF(x, visibleRect.bottom()));
    }
    
    // Grid Lines (Horizontal - Tracks) — ビューポート内の行のみ
    int firstRowIdx = qMax(0, visibleRect.top() / rowHeight);
    int lastRowIdx = qMin(numRows - 1, visibleRect.bottom() / rowHeight);
    for (int i = firstRowIdx; i <= lastRowIdx; ++i) {
        double y = i * rowHeight + rowHeight;
        p.setPen(ThemeManager::instance().gridLineSubColor());
        p.drawLine(QPointF(visibleLeft, y), QPointF(visibleRight, y));
    }
    
    // Clips
    drawClips(p, visibleRect);

    // ── エクスポート範囲表示 ──
    if (m_project && m_project->exportStartBar() >= 0 && m_project->exportEndBar() > m_project->exportStartBar()) {
        int x1 = static_cast<int>(m_project->exportStartBar() * pixelsPerBar());
        int x2 = static_cast<int>(m_project->exportEndBar() * pixelsPerBar());
        // 範囲外をグレーアウト
        p.fillRect(0, 0, x1, widgetHeight, ThemeManager::instance().isDarkMode() ? QColor(0, 0, 0, 80) : QColor(0, 0, 0, 15));
        p.fillRect(x2, 0, widgetWidth - x2, widgetHeight, ThemeManager::instance().isDarkMode() ? QColor(0, 0, 0, 80) : QColor(0, 0, 0, 15));
        // 範囲の境界線
        p.setPen(QPen(QColor(59, 130, 246, 120), 1, Qt::DashLine));
        p.drawLine(x1, 0, x1, widgetHeight);
        p.drawLine(x2, 0, x2, widgetHeight);
    }

    // はじけ飛ぶ削除エフェクト
    drawBurstEffects(p);

    // 斬撃分割エフェクト
    drawSlashEffects(p);

    // MIDIドロップ波リビールエフェクト（クリップ形状で描画）
    for (const WaveReveal& wave : m_waveReveals) {
        drawWaveRevealClip(p, wave);
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
    p.drawLine(QPointF(playheadX, 0), QPointF(playheadX, widgetHeight));
    p.setRenderHint(QPainter::Antialiasing, false);

    // ラバーバンド範囲選択の描画
    if (m_isRubberBanding && m_rubberBandRect.isValid()) {
        p.setPen(QPen(QColor(59, 130, 246, 180), 1));
        p.setBrush(QColor(59, 130, 246, 40));
        p.drawRect(m_rubberBandRect);
    }
}

void ArrangementGridWidget::drawClips(QPainter& p, const QRect& visibleRect)
{
    if (!m_project) return;

    int rowHeight = 100;
    const QList<Track*>& visTracks = visibleTracks();
    
    for (int i = 0; i < visTracks.size(); ++i) {
        Track* track = visTracks.at(i);
        if (!track) continue;
        
        // フォルダトラック → 子トラックのクリップを統合表示
        if (track->isFolder()) {
            drawFolderSummary(p, track, i * rowHeight, rowHeight, visibleRect);
            continue;
        }
        
        int y = i * rowHeight;
        
        for (Clip* clip : track->clips()) {
            // 波リビールアニメーション中のクリップは drawWaveRevealClip で描画するのでスキップ
            bool isWaveRevealing = false;
            for (const WaveReveal& wr : m_waveReveals) {
                if (wr.clipId == clip->id()) {
                    isWaveRevealing = true;
                    break;
                }
            }
            if (isWaveRevealing) continue;

            double x = clip->startTick() * pixelsPerTick();
            double w = clip->durationTicks() * pixelsPerTick();
            
            // ドラッグ中のクリップは移動先トラックの位置に描画
            double drawY = y;
            if (m_isDragging && clip->id() == m_selectedClipId 
                && m_dragCurrentTrackIndex >= 0 && m_dragCurrentTrackIndex != i) {
                drawY = m_dragCurrentTrackIndex * rowHeight;
            }
            
            QRectF clipRect(x, drawY + 10, w, rowHeight - 20);

            // 可視範囲外のクリップ描画をスキップ
            if (clipRect.right() < visibleRect.left() || clipRect.left() > visibleRect.right()) {
                continue;
            }
            
            // アニメーション状態を取得
            float animScale = 1.0f;
            float animOpacity = 1.0f;
            float glowIntensity = 0.0f;

            if (m_clipAnims.contains(clip->id())) {
                const ClipAnim& anim = m_clipAnims[clip->id()];
                if (anim.type == ClipAnim::PopIn) {
                    float t = anim.progress;
                    animScale = 0.85f + 0.15f * BurstAnimation::easeOutBack(t);
                    animOpacity = BurstAnimation::easeOutCubic(t);
                } else if (anim.type == ClipAnim::SelectPulse) {
                    float t = anim.progress;
                    // 柔らかいパルス: 少し膨らんで戻る
                    glowIntensity = (1.0f - t) * 0.6f;
                    animScale = 1.0f + 0.03f * (1.0f - t);
                }
            }

            // スケール変換を適用
            p.save();
            if (!qFuzzyCompare(animScale, 1.0f)) {
                QPointF center = clipRect.center();
                p.translate(center);
                p.scale(animScale, animScale);
                p.translate(-center);
            }
            p.setOpacity(animOpacity);
            
            // トラックカラーを使用してクリップを描画
            bool isSelected = (clip->id() == m_selectedClipId || m_selectedClipIds.contains(clip->id()));
            QColor trackColor = track->color();
            QColor fillColor = isSelected ? trackColor : trackColor.lighter(140);
            QColor borderColor = isSelected ? trackColor.darker(130) : trackColor;

            // 選択グロー
            if (isSelected && glowIntensity > 0.01f) {
                p.setRenderHint(QPainter::Antialiasing, true);
                QColor glow = trackColor;
                glow.setAlphaF(glowIntensity * 0.4f);
                double expand = 4.0 * glowIntensity;
                QRectF glowRect = clipRect.adjusted(-expand, -expand, expand, expand);
                p.setPen(Qt::NoPen);
                p.setBrush(glow);
                p.drawRoundedRect(glowRect, 6, 6);
                p.setRenderHint(QPainter::Antialiasing, false);
            }

            p.setRenderHint(QPainter::Antialiasing, true);
            p.setBrush(fillColor);
            p.setPen(QPen(borderColor, isSelected ? 2 : 1));
            p.drawRoundedRect(clipRect, 4, 4);
            p.setRenderHint(QPainter::Antialiasing, false);
            
            // クリップ内のコンテンツ描画（MIDIノート or オーディオ波形）
            if (clip->isAudioClip()) {
                // オーディオクリップ → 波形を描画
                QColor waveColor = isSelected ? QColor(255, 255, 255, 200) : trackColor.darker(120);
                drawAudioWaveform(p, clipRect, clip, waveColor);
            } else if (!clip->notes().isEmpty()) {
                double clipH = clipRect.height() - 4;
                double clipW = clipRect.width() - 4;
                double clipX = clipRect.x() + 2;
                double clipY = clipRect.y() + 2;
                
                QColor noteColor = isSelected ? QColor(255, 255, 255, 200) : trackColor.darker(120);
                p.setPen(Qt::NoPen);
                p.setBrush(noteColor);
                
                qint64 clipDuration = clip->durationTicks();
                if (clipDuration <= 0) clipDuration = 1;

                // クリップ領域にクリッピングして、はみ出しノートを描画させない
                p.save();
                p.setClipRect(QRectF(clipX, clipY, clipW, clipH).toAlignedRect());
                p.setRenderHint(QPainter::Antialiasing, true);
                
                for (Note* note : clip->notes()) {
                    // 絶対位置ベースで描画（比例ではなく tick→pixel 変換）
                    double noteX = clipX + note->startTick() * pixelsPerTick();
                    double noteW = qMax(2.0, note->durationTicks() * pixelsPerTick());

                    // クリップ範囲外のノートはスキップ
                    if (noteX >= clipX + clipW || noteX + noteW <= clipX) continue;
                    
                    double pitchRatio = 1.0 - (static_cast<double>(note->pitch()) / 127.0);
                    double noteH = qMax(2.0, clipH / 16.0);
                    double noteY = clipY + pitchRatio * (clipH - noteH);
                    
                    p.drawRect(QRectF(noteX, noteY, noteW, noteH));
                }

                p.restore();
            }
            
            p.restore();
        }
    }

    // ── フェードアウト中の削除済みクリップを描画 ──
    for (auto it = m_fadingClips.begin(); it != m_fadingClips.end(); ++it) {
        int clipId = it.key();
        Clip* clip = it.value();
        if (!clip) continue;

        // すでにトラックに戻っている（再追加された）ならスキップ（重複描画防止）
        bool existsInModel = false;
        for (Track* t : visTracks) {
            if (t->clips().contains(clip)) {
                existsInModel = true;
                break;
            }
        }
        if (existsInModel) continue;

        Track* track = qobject_cast<Track*>(clip->parent());
        if (!track) continue;
        int trackIdx = visibleTrackIndex(track);
        if (trackIdx < 0) continue;

        double x = clip->startTick() * pixelsPerTick();
        double w = clip->durationTicks() * pixelsPerTick();
        double y = trackIdx * rowHeight;
        QRectF clipRect(x, y + 10, w, rowHeight - 20);

        float opacity = 0.5f; // デフォルト
        if (m_clipAnims.contains(clipId)) {
            const ClipAnim& anim = m_clipAnims[clipId];
            if (anim.type == ClipAnim::FadeOut) {
                opacity = 1.0f - anim.progress;
            }
        }

        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setOpacity(opacity);
        QColor trackColor = track->color();
        p.setBrush(trackColor.lighter(140));
        p.setPen(QPen(trackColor, 1));
        p.drawRoundedRect(clipRect, 4, 4);
        p.setRenderHint(QPainter::Antialiasing, false);

        // クリップ内ノートの簡易描画（フェードアウト版）
        if (!clip->notes().isEmpty()) {
            QColor noteColor = trackColor.darker(120);
            noteColor.setAlpha(150);
            p.setPen(Qt::NoPen);
            p.setBrush(noteColor);
            double clipDrawX = clipRect.x() + 2;
            double clipDrawY = clipRect.y() + 2;
            double clipW = clipRect.width() - 4;
            double clipH = clipRect.height() - 4;
            qint64 clipDuration = clip->durationTicks();
            if (clipDuration > 0) {
                p.setClipRect(clipRect.adjusted(2,2,-2,-2).toAlignedRect());
                p.setRenderHint(QPainter::Antialiasing, true);
                for (Note* note : clip->notes()) {
                    double noteX = clipDrawX + note->startTick() * pixelsPerTick();
                    double noteW = qMax(2.0, note->durationTicks() * pixelsPerTick());
                    double pitchRatio = 1.0 - (static_cast<double>(note->pitch()) / 127.0);
                    double noteH = qMax(2.0, clipH / 16.0);
                    double noteY = clipDrawY + pitchRatio * (clipH - noteH);
                    p.drawRect(QRectF(noteX, noteY, noteW, noteH));
                }
            }
        }
        p.restore();
    }
}

void ArrangementGridWidget::drawFolderSummary(QPainter& p, Track* folder, int y, int rowHeight,
                                              const QRect& visibleRect)
{
    if (!m_project || !folder || !folder->isFolder()) return;

    const FolderSummaryCacheEntry* cache = folderSummaryCacheForTrack(folder);
    if (!cache || !cache->hasAnyClip) return;

    // 統合クリップの矩形
    double clipX = cache->minTick * pixelsPerTick();
    double clipW = qMax(4.0, (cache->maxTick - cache->minTick) * pixelsPerTick());
    double clipTop = y + 10.0;
    double clipH = rowHeight - 20.0;
    QRectF clipRect(clipX, clipTop, clipW, clipH);

    // 可視範囲外なら描画しない
    if (clipRect.right() < visibleRect.left() || clipRect.left() > visibleRect.right()) {
        return;
    }

    // フォルダカラーで統合クリップ背景を描画
    QColor folderColor = folder->color();
    QColor fillColor = folderColor.lighter(150);
    fillColor.setAlpha(120);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(folderColor, 1));
    p.setBrush(fillColor);
    p.drawRoundedRect(clipRect, 4, 4);

    // クリップ内にノートをまとめて描画（各子トラックの色で）
    double innerX = clipRect.x() + 2.0;
    double innerW = clipRect.width() - 4.0;
    double innerY = clipRect.y() + 2.0;
    double innerH = clipRect.height() - 4.0;
    if (innerH < 2.0 || innerW < 2.0) {
        p.restore();
        return;
    }

    p.setClipRect(QRectF(innerX, innerY, innerW, innerH).toAlignedRect());
    p.setPen(Qt::NoPen);

    for (const FolderSummaryClipEntry& clipEntry : cache->clips) {
        double childClipX = (clipEntry.startTick - cache->minTick) * pixelsPerTick();
        double childClipW = qMax(1.0, (clipEntry.endTick - clipEntry.startTick) * pixelsPerTick());
        if (innerX + childClipX + childClipW < visibleRect.left() ||
            innerX + childClipX > visibleRect.right()) {
            continue;
        }

        for (const FolderSummaryNoteEntry& noteEntry : clipEntry.notes) {
            p.setBrush(noteEntry.color);

            double noteX = innerX + (noteEntry.startTick - cache->minTick) * pixelsPerTick();
            double noteW = qMax(1.0, noteEntry.durationTicks * pixelsPerTick());
            if (noteX >= innerX + innerW || noteX + noteW <= innerX) continue;
            if (noteX + noteW < visibleRect.left() || noteX > visibleRect.right()) continue;

            double pitchRatio = 1.0 - (static_cast<double>(noteEntry.pitch) / 127.0);
            double noteH = qMax(2.0, innerH / 16.0);
            double noteY = innerY + pitchRatio * (innerH - noteH);

            p.drawRect(QRectF(noteX, noteY, noteW, noteH));
        }
    }

    p.restore();
}

void ArrangementGridWidget::drawAudioWaveform(QPainter& p, const QRectF& clipRect,
                                               Clip* clip, const QColor& waveColor)
{
    if (!clip || !clip->isAudioClip()) return;

    const QVector<float>& preview = clip->waveformPreview();
    if (preview.isEmpty()) return;

    double clipX = clipRect.x() + 2.0;
    double clipW = clipRect.width() - 4.0;
    double clipY = clipRect.y() + 2.0;
    double clipH = clipRect.height() - 4.0;
    if (clipW <= 0 || clipH <= 0) return;

    const int pixelWidth = qMax(1, static_cast<int>(clipW));
    const int pixelHeight = qMax(1, static_cast<int>(clipH));
    const QRgb colorKey = waveColor.rgba();

    auto& cache = m_waveformCache[clip->id()];
    if (cache.width != pixelWidth || cache.height != pixelHeight ||
        cache.color != colorKey || cache.image.isNull()) {
        // 波形の棒グラフを画像として保持し、通常の再描画では drawImage だけで済ませる。
        cache.width = pixelWidth;
        cache.height = pixelHeight;
        cache.color = colorKey;
        cache.image = QImage(pixelWidth, pixelHeight, QImage::Format_ARGB32_Premultiplied);
        cache.image.fill(Qt::transparent);

        QPainter cachePainter(&cache.image);
        cachePainter.setPen(Qt::NoPen);
        cachePainter.setBrush(waveColor);

        const int previewSize = preview.size();
        const double centerY = pixelHeight / 2.0;
        const double halfHeight = pixelHeight / 2.0;
        for (int px = 0; px < pixelWidth; ++px) {
            int idx = static_cast<int>(static_cast<qint64>(px) * previewSize / pixelWidth);
            idx = qBound(0, idx, previewSize - 1);

            double barH = qMax(1.0, preview[idx] * halfHeight);
            cachePainter.drawRect(QRectF(px, centerY - barH, 1.0, barH * 2.0));
        }
    }

    p.drawImage(QPointF(clipX, clipY), cache.image);
}
