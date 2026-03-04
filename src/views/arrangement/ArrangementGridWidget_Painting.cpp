/**
 * @file ArrangementGridWidget_Painting.cpp
 * @brief ArrangementGridWidget の描画処理（paintEvent, drawClips, drawFolderSummary, drawAudioWaveform）
 */
#include "ArrangementGridWidget.h"
#include "models/Project.h"
#include "models/Track.h"
#include "models/Clip.h"
#include "models/Note.h"
#include <QPainter>
#include <QPainterPath>
#include <QPainterPath>
#include "common/Constants.h"
#include "common/BurstAnimationHelper.h"
#include "common/ThemeManager.h"

using namespace Darwin;

void ArrangementGridWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    int rowHeight = 100;
    double ppBar = pixelsPerBar();
    double ppBeat = ppBar / BEATS_PER_BAR;
    
    int numRows = (m_project) ? visibleTracks().size() : 0;
    // Ensure we draw at least a few empty rows if no tracks
    if (numRows == 0) numRows = 4;

    int widgetWidth = width();
    int widgetHeight = numRows * rowHeight;
    if (widgetHeight < height()) widgetHeight = height(); // Fill
    
    
    // Background
    p.fillRect(rect(), ThemeManager::instance().backgroundColor());
    
    // Grid Lines (Vertical)
    // ズームレベルに応じて細分化:
    //   1拍(4分音符) = ppBeat px
    //   8分音符 = ppBeat/2, 16分 = ppBeat/4, 32分 = ppBeat/8
    // 各レベルの線は、そのピクセル幅が一定以上のとき描画する
    const int MIN_SUBDIV_PX = 12; // この幅未満の細分化線は非表示
    
    // 最も細かい分割数を決定（1拍あたり何本の線を引くか）
    int subdivPerBeat = 1; // デフォルト: 4分音符のみ
    if (ppBeat / 2 >= MIN_SUBDIV_PX)  subdivPerBeat = 2;  // 8分音符
    if (ppBeat / 4 >= MIN_SUBDIV_PX)  subdivPerBeat = 4;  // 16分音符
    if (ppBeat / 8 >= MIN_SUBDIV_PX)  subdivPerBeat = 8;  // 32分音符
    
    int subdivsPerBar = BEATS_PER_BAR * subdivPerBeat;
    
    // 各線の位置を独立計算（累積誤差を防止してTimelineWidgetと一致させる）
    int totalSubdivs = static_cast<int>(widgetWidth * subdivsPerBar / ppBar) + 1;
    for (int idx = 0; idx <= totalSubdivs; ++idx) {
        double x = idx * ppBar / subdivsPerBar;
        if (x >= widgetWidth) break;
        
        if (idx % subdivsPerBar == 0) {
            // 小節線 — 最も濃い
            p.setPen(ThemeManager::instance().gridLineColor());
        } else if (idx % subdivPerBeat == 0) {
            // 拍線（4分音符）
            p.setPen(ThemeManager::instance().gridLineSubColor());
        } else if (subdivPerBeat >= 4 && idx % (subdivPerBeat / 2) == 0) {
            // 8分音符線
            p.setPen(ThemeManager::instance().gridLineSubBeatColor());
        } else {
            // 16分/32分音符線 — 最も薄い
            p.setPen(ThemeManager::instance().gridLineTickColor());
        }
        p.drawLine(QPointF(x, 0), QPointF(x, widgetHeight));
    }
    
    // Grid Lines (Horizontal - Tracks)
    QList<Track*> visTracks = m_project ? visibleTracks() : QList<Track*>();
    for(int i = 0; i < numRows; ++i) {
        double y = i * rowHeight + rowHeight;
        
        p.setPen(ThemeManager::instance().gridLineSubColor());
        p.drawLine(QPointF(0, y), QPointF(widgetWidth, y));
    }
    
    // Clips
    drawClips(p);

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
    p.setPen(QPen(QColor("#FF3366"), 2));
    p.drawLine(QPointF(playheadX, 0), QPointF(playheadX, widgetHeight));

    // ラバーバンド範囲選択の描画
    if (m_isRubberBanding && m_rubberBandRect.isValid()) {
        p.setPen(QPen(QColor(59, 130, 246, 180), 1));
        p.setBrush(QColor(59, 130, 246, 40));
        p.drawRect(m_rubberBandRect);
    }
}

void ArrangementGridWidget::drawClips(QPainter& p)
{
    if (!m_project) return;

    int rowHeight = 100;
    QList<Track*> visTracks = visibleTracks();
    
    for (int i = 0; i < visTracks.size(); ++i) {
        Track* track = visTracks.at(i);
        if (!track) continue;
        
        // フォルダトラック → 子トラックのクリップを統合表示
        if (track->isFolder()) {
            drawFolderSummary(p, track, i * rowHeight, rowHeight);
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

void ArrangementGridWidget::drawFolderSummary(QPainter& p, Track* folder, int y, int rowHeight)
{
    if (!m_project || !folder || !folder->isFolder()) return;

    QList<Track*> children = m_project->folderDescendants(folder);
    if (children.isEmpty()) return;

    // 全子トラックのクリップから統合範囲を算出
    qint64 minTick = std::numeric_limits<qint64>::max();
    qint64 maxTick = 0;
    bool hasAnyClip = false;

    for (Track* child : children) {
        for (Clip* clip : child->clips()) {
            if (clip->startTick() < minTick) minTick = clip->startTick();
            if (clip->endTick() > maxTick) maxTick = clip->endTick();
            hasAnyClip = true;
        }
    }
    if (!hasAnyClip) return;

    // 統合クリップの矩形
    double clipX = minTick * pixelsPerTick();
    double clipW = qMax(4.0, (maxTick - minTick) * pixelsPerTick());
    double clipTop = y + 10.0;
    double clipH = rowHeight - 20.0;
    QRectF clipRect(clipX, clipTop, clipW, clipH);

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

    for (Track* child : children) {
        QColor noteColor = child->color().darker(110);
        noteColor.setAlpha(180);
        p.setBrush(noteColor);

        for (Clip* clip : child->clips()) {
            for (Note* note : clip->notes()) {
                double noteX = innerX + (clip->startTick() + note->startTick()) * pixelsPerTick() - clipX;
                double noteW = qMax(1.0, note->durationTicks() * pixelsPerTick());
                if (noteX >= innerX + innerW || noteX + noteW <= innerX) continue;

                double pitchRatio = 1.0 - (static_cast<double>(note->pitch()) / 127.0);
                double noteH = qMax(2.0, innerH / 16.0);
                double noteY = innerY + pitchRatio * (innerH - noteH);

                p.drawRect(QRectF(noteX, noteY, noteW, noteH));
            }
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

    double centerY = clipY + clipH / 2.0;
    double halfHeight = clipH / 2.0;

    p.save();
    p.setClipRect(QRectF(clipX, clipY, clipW, clipH).toAlignedRect());
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(waveColor);

    // プレビューデータからクリップ幅に合わせて描画
    int previewSize = preview.size();
    // 描画する棒の数は幅から決定
    int steps = static_cast<int>(clipW);
    for (int px = 0; px < steps; ++px) {
        // ピクセル位置 → プレビューインデックス
        int idx = static_cast<int>(static_cast<qint64>(px) * previewSize / steps);
        idx = qBound(0, idx, previewSize - 1);

        float peak = preview[idx];
        double barH = peak * halfHeight;
        if (barH < 1.0) barH = 1.0;

        // 中央から上下対称に描画
        p.drawRect(QRectF(clipX + px, centerY - barH, 1.0, barH * 2.0));
    }

    p.restore();
}
