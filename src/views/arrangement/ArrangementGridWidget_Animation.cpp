/**
 * @file ArrangementGridWidget_Animation.cpp
 * @brief ArrangementGridWidget のアニメーション処理（クリップアニメ、バースト、波リビール、斬撃分割）
 */
#include "ArrangementGridWidget.h"
#include "models/Project.h"
#include "models/Track.h"
#include "models/Clip.h"
#include "models/Note.h"
#include <QPainter>
#include <QtMath>
#include "common/Constants.h"
#include "common/BurstAnimationHelper.h"

using namespace Darwin;

// ===== アニメーション実装 =====

void ArrangementGridWidget::startClipAnim(int clipId, ClipAnim::Type type)
{
    ClipAnim anim;
    anim.startMs = m_animClock.elapsed();
    anim.progress = 0.0f;
    anim.type = type;
    m_clipAnims[clipId] = anim;
    
    // PopIn の場合はフェードアウト用の退避リストから除去（再追加された場合など）
    if (type != ClipAnim::FadeOut) {
        m_fadingClips.remove(clipId);
    }

    if (!m_animTimer.isActive()) {
        m_animTimer.start();
    }
}

void ArrangementGridWidget::startBurstAnim(const QRectF& rect, const QColor& color, int trackIndex)
{
    BurstAnimation::spawnBurst(rect, color, trackIndex,
                               m_animClock.elapsed(),
                               BurstAnimation::arrangementParams(),
                               m_burstGhosts, m_particles);
    if (!m_animTimer.isActive()) {
        m_animTimer.start();
    }
}

// 速度ベースの波展開距離（クリップサイズに依存しない一定ペースで広がる）
// 600px/sec + 加速150px/sec²: ビューポート幅(≈1200px)を約1.5秒で通過
float ArrangementGridWidget::computeWaveSpread(float elapsedMs)
{
    float sec = elapsedMs / 1000.0f;
    return 600.0f * sec + 150.0f * sec * sec;
}

void ArrangementGridWidget::startWaveReveal(int clipId, const QPointF& dropPoint)
{
    // クリップのピクセル範囲からmaxDistを事前計算
    float maxDist = 1.0f;
    if (m_project) {
        for (int i = 0; i < m_project->trackCount(); ++i) {
            Track* t = m_project->trackAt(i);
            for (Clip* c : t->clips()) {
                if (c->id() == clipId) {
                    float cx = static_cast<float>(c->startTick() * pixelsPerTick());
                    float cw = static_cast<float>(c->durationTicks() * pixelsPerTick());
                    float dX = static_cast<float>(dropPoint.x());
                    maxDist = qMax(dX - cx, cx + cw - dX);
                    if (maxDist < 1.0f) maxDist = 1.0f;
                    break;
                }
            }
        }
    }

    WaveReveal wave;
    wave.clipId    = clipId;
    wave.dropPoint = dropPoint;
    wave.startMs   = m_animClock.elapsed();
    wave.progress  = 0.0f;
    wave.maxDist   = maxDist;
    m_waveReveals.append(wave);

    if (!m_animTimer.isActive()) {
        m_animTimer.start();
    }
}

void ArrangementGridWidget::drawWaveRevealClip(QPainter& p, const WaveReveal& wave)
{
    if (!m_project) return;

    // 対象クリップとトラックを見つける
    Clip* clip = nullptr;
    Track* track = nullptr;
    int trackIndex = -1;
    for (int i = 0; i < m_project->trackCount(); ++i) {
        Track* t = m_project->trackAt(i);
        for (Clip* c : t->clips()) {
            if (c->id() == wave.clipId) {
                clip = c;
                track = t;
                trackIndex = i;
                break;
            }
        }
        if (clip) break;
    }
    if (!clip || !track) return;

    const int rowHeight = ARRANGEMENT_TRACK_ROW_HEIGHT;
    int x = static_cast<int>(clip->startTick() * pixelsPerTick());
    int w = static_cast<int>(clip->durationTicks() * pixelsPerTick());
    int y = trackIndex * rowHeight;
    QRect clipRect(x, y + 10, w, rowHeight - 20);

    // 速度ベースの展開距離をリアルタイム計算
    qint64 now = m_animClock.elapsed();
    float elapsedMs = static_cast<float>(now - wave.startMs);
    float currentSpread = computeWaveSpread(elapsedMs);

    // 展開率（エッジの透明度などに使用）
    float revealRatio = qBound(0.0f, currentSpread / wave.maxDist, 1.0f);

    // ドロップ位置からクリップの左端・右端までの距離を計算
    float dropX = static_cast<float>(wave.dropPoint.x());
    float clipLeft = static_cast<float>(clipRect.left());
    float clipRight = static_cast<float>(clipRect.right());
    float revealLeft  = dropX - currentSpread;
    float revealRight = dropX + currentSpread;

    // クリップ領域でクランプ
    revealLeft  = qMax(revealLeft,  clipLeft);
    revealRight = qMin(revealRight, clipRight);

    // progress=0 でも最低限ドロップ地点に光る縦線を描画（アニメーション開始を視覚的に示す）
    if (revealRight <= revealLeft) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        float clampedDropX = qBound(clipLeft, dropX, clipRight);
        QColor startGlow(168, 85, 247, 200); // パープルの光線
        p.setPen(QPen(startGlow, 3.0f));
        p.drawLine(QPointF(clampedDropX, clipRect.top()), QPointF(clampedDropX, clipRect.bottom()));
        p.restore();
        return;
    }

    // ── 波の色帯（複数色）定義 ──
    const QList<QColor> waveColors = {
        QColor(99, 102, 241),    // インディゴー
        QColor(168, 85, 247),    // パープル
        QColor(59, 130, 246),    // ブルー
        QColor(34, 211, 238),    // シアン
        QColor(16, 185, 129),    // エメラルド
    };

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // ── 1. まずクリッピングして通過部分だけクリップ本体を描画 ──
    QRectF revealRect(revealLeft, clipRect.top(), revealRight - revealLeft, clipRect.height());
    p.setClipRect(revealRect);

    bool isSelected = (clip->id() == m_selectedClipId);
    QColor trackColor = track->color();
    QColor fillColor = isSelected ? trackColor : trackColor.lighter(140);
    QColor borderColor = isSelected ? trackColor.darker(130) : trackColor;

    p.setOpacity(1.0f);
    p.setBrush(fillColor);
    p.setPen(QPen(borderColor, isSelected ? 2 : 1));
    p.drawRoundedRect(clipRect, 4, 4);

    // ノートのミニプレビュー
    if (!clip->notes().isEmpty()) {
        int clipH = clipRect.height() - 4;
        int clipW = clipRect.width() - 4;
        int clipDrawX = clipRect.x() + 2;
        int clipDrawY = clipRect.y() + 2;

        QColor noteColor = isSelected ? QColor(255, 255, 255, 200) : trackColor.darker(120);
        p.setPen(Qt::NoPen);
        p.setBrush(noteColor);

        qint64 clipDuration = clip->durationTicks();
        if (clipDuration <= 0) clipDuration = 1;

        for (Note* note : clip->notes()) {
            double noteXRatio = static_cast<double>(note->startTick()) / clipDuration;
            double noteWRatio = static_cast<double>(note->durationTicks()) / clipDuration;
            int noteX = clipDrawX + static_cast<int>(noteXRatio * clipW);
            int noteW = qMax(2, static_cast<int>(noteWRatio * clipW));

            double pitchRatio = 1.0 - (static_cast<double>(note->pitch()) / 127.0);
            int noteH = qMax(2, clipH / 16);
            int noteY = clipDrawY + static_cast<int>(pitchRatio * (clipH - noteH));

            p.drawRect(noteX, noteY, noteW, noteH);
        }
    }

    // ── 2. クリップ描画後に波エッジをオーバーレイ描画 ──
    p.setClipping(false);

    float waveWidth = 18.0f * (1.0f - revealRatio * 0.3f); // 波幅
    float edgeAlpha = qMax(0.0f, 1.0f - revealRatio * 0.5f); // 波エッジの透明度

    // 右方向の波エッジ
    if (edgeAlpha > 0.01f && revealRight < clipRight) {
        for (int i = waveColors.size() - 1; i >= 0; --i) {
            float offset = i * 6.0f;
            float edgeX = revealRight - offset;
            if (edgeX < revealLeft) break;

            QColor edgeColor = waveColors[i];
            float alpha = edgeAlpha * (1.0f - i * 0.12f);
            edgeColor.setAlphaF(qMax(0.0f, alpha * 0.9f));

            QRectF edgeRect(edgeX, clipRect.top(), waveWidth, clipRect.height());
            edgeRect = edgeRect.intersected(QRectF(clipRect));
            if (edgeRect.width() > 0) {
                p.setPen(Qt::NoPen);
                p.setBrush(edgeColor);
                p.drawRoundedRect(edgeRect, 2, 2);
            }
        }
    }

    // 左方向の波エッジ
    if (edgeAlpha > 0.01f && revealLeft > clipLeft) {
        for (int i = waveColors.size() - 1; i >= 0; --i) {
            float offset = i * 6.0f;
            float edgeX = revealLeft + offset;
            if (edgeX > revealRight) break;

            QColor edgeColor = waveColors[i];
            float alpha = edgeAlpha * (1.0f - i * 0.12f);
            edgeColor.setAlphaF(qMax(0.0f, alpha * 0.9f));

            QRectF edgeRect(edgeX - waveWidth, clipRect.top(), waveWidth, clipRect.height());
            edgeRect = edgeRect.intersected(QRectF(clipRect));
            if (edgeRect.width() > 0) {
                p.setPen(Qt::NoPen);
                p.setBrush(edgeColor);
                p.drawRoundedRect(edgeRect, 2, 2);
            }
        }
    }

    p.restore();
}

void ArrangementGridWidget::drawBurstEffects(QPainter& p)
{
    BurstAnimation::drawBurstEffects(p, m_burstGhosts, m_particles,
                                     BurstAnimation::arrangementParams().roundedRadius);
}

void ArrangementGridWidget::tickAnimations()
{
    qint64 now = m_animClock.elapsed();
    bool anyActive = false;
    
    // 軌跡のフェードアニメーション
    const float fadeStep = PLAYHEAD_TRAIL_FADE_STEP;
    if (m_isPlaying) {
        if (m_trailOpacity < 1.0f) {
            m_trailOpacity = qMin(1.0f, m_trailOpacity + fadeStep);
            anyActive = true;
        }
    } else {
        if (m_trailOpacity > 0.0f) {
            m_trailOpacity = qMax(0.0f, m_trailOpacity - fadeStep);
            anyActive = true;
        }
    }

    QList<int> toRemove;
    for (auto it = m_clipAnims.begin(); it != m_clipAnims.end(); ++it) {
        ClipAnim& anim = it.value();
        float durationMs = (anim.type == ClipAnim::PopIn) ? 280.0f : 350.0f;
        float elapsed = static_cast<float>(now - anim.startMs);
        anim.progress = qBound(0.0f, elapsed / durationMs, 1.0f);
        
        if (anim.progress >= 1.0f) {
            toRemove.append(it.key());
        } else {
            anyActive = true;
        }
    }
    
    for (int id : toRemove) {
        m_clipAnims.remove(id);
        m_fadingClips.remove(id);
    }
    
    // バースト共通エンジンで更新
    if (BurstAnimation::tickBurst(now, BurstAnimation::arrangementParams(),
                                   m_burstGhosts, m_particles)) {
        anyActive = true;
    }

    // 波リビールアニメーションの更新（速度ベース: spread >= maxDist で完了）
    for (int i = m_waveReveals.size() - 1; i >= 0; --i) {
        WaveReveal& wave = m_waveReveals[i];
        float elapsed = static_cast<float>(now - wave.startMs);
        float spread = computeWaveSpread(elapsed);
        wave.progress = qBound(0.0f, spread / wave.maxDist, 1.0f);
        if (spread >= wave.maxDist) {
            m_waveReveals.removeAt(i);
        } else {
            anyActive = true;
        }
    }

    // 斬撃アニメーションの更新
    for (int i = m_slashAnims.size() - 1; i >= 0; --i) {
        SlashAnim& slash = m_slashAnims[i];
        float elapsed = static_cast<float>(now - slash.startMs);
        float durationMs = 500.0f;
        slash.progress = qBound(0.0f, elapsed / durationMs, 1.0f);
        if (slash.progress >= 1.0f) {
            m_slashAnims.removeAt(i);
        } else {
            anyActive = true;
        }
    }
    
    if (!anyActive && m_clipAnims.isEmpty()) {
        m_animTimer.stop();
    }
    
    update();
}

// ===== 長押し分割 =====

void ArrangementGridWidget::onLongPressConfirmed()
{
    if (!m_project || m_longPressClipId < 0) return;

    // ドラッグ操作を強制キャンセル（分割を優先）
    m_isDragging = false;
    m_isResizing = false;
    m_isResizingLeft = false;

    const int rowHeight = ARRANGEMENT_TRACK_ROW_HEIGHT;
    QList<Track*> visTracks_ = visibleTracks();

    // 対象クリップとトラックを探す
    Track* targetTrack = nullptr;
    Clip* targetClip = nullptr;
    int trackIdx = -1;
    for (int i = 0; i < visTracks_.size(); ++i) {
        Track* track = visTracks_.at(i);
        if (!track || track->isFolder()) continue;
        for (Clip* clip : track->clips()) {
            if (clip->id() == m_longPressClipId) {
                targetTrack = track;
                targetClip = clip;
                trackIdx = i;
                break;
            }
        }
        if (targetClip) break;
    }

    if (!targetTrack || !targetClip) return;

    // 長押し位置からtickを算出（スナップ適用）
    qint64 splitTick = static_cast<qint64>(m_longPressPos.x() / pixelsPerTick());
    splitTick = snapTick(splitTick);
    // クリップ内の相対tick
    qint64 relSplitTick = splitTick - targetClip->startTick();

    // クリップの端付近では分割しない（最低1ビート分の余白を確保）
    if (relSplitTick <= TICKS_PER_BEAT / 2 || relSplitTick >= targetClip->durationTicks() - TICKS_PER_BEAT / 2) {
        m_longPressClipId = -1;
        return;
    }

    // 斬撃アニメーションを起動
    float slashCenterY = trackIdx * rowHeight + rowHeight * 0.5f;
    QPointF slashCenter(m_longPressPos.x(), slashCenterY);
    startSlashAnim(slashCenter, static_cast<float>(rowHeight), targetTrack->color());

    // クリップ分割を実行
    splitClipAt(targetTrack, targetClip, relSplitTick);

    m_longPressClipId = -1;
    m_selectedClipId = -1;
    emit clipSelected(nullptr);
    update();
}

void ArrangementGridWidget::splitClipAt(Track* track, Clip* clip, qint64 relSplitTick)
{
    if (!track || !clip || relSplitTick <= 0 || relSplitTick >= clip->durationTicks()) return;

    qint64 origStart = clip->startTick();
    qint64 origDuration = clip->durationTicks();

    // ── 後半クリップを先に作成 ──
    qint64 newStart = origStart + relSplitTick;
    qint64 newDuration = origDuration - relSplitTick;
    Clip* newClip = track->addClip(newStart, newDuration);

    if (clip->isAudioClip()) {
        // オーディオクリップの分割: PCMサンプルを分割する
        // tick→サンプル位置の変換
        double bpm = m_project ? m_project->bpm() : 120.0;
        double ticksPerSecond = bpm * TICKS_PER_BEAT / 60.0;
        double splitSeconds = static_cast<double>(relSplitTick) / ticksPerSecond;
        qint64 splitSample = static_cast<qint64>(splitSeconds * clip->audioSampleRate());

        const QVector<float>& srcL = clip->audioSamplesL();
        const QVector<float>& srcR = clip->audioSamplesR();

        // 後半のオーディオデータ
        QVector<float> newL, newR;
        if (splitSample < srcL.size()) {
            newL = srcL.mid(static_cast<int>(splitSample));
            newR = srcR.mid(static_cast<int>(splitSample));
        }
        newClip->setAudioData(newL, newR, clip->audioSampleRate(), clip->audioFilePath());

        // 前半のオーディオデータをトリム
        QVector<float> trimL = srcL.mid(0, static_cast<int>(splitSample));
        QVector<float> trimR = srcR.mid(0, static_cast<int>(splitSample));
        clip->setAudioData(trimL, trimR, clip->audioSampleRate(), clip->audioFilePath());
    } else {
        // MIDIクリップのノート分割
        QList<Note*> toRemove;
        for (Note* note : clip->notes()) {
            qint64 noteStart = note->startTick();
            qint64 noteEnd = noteStart + note->durationTicks();

            if (noteStart >= relSplitTick) {
                // ノート全体が後半 → 後半クリップへ移動
                newClip->addNote(note->pitch(), noteStart - relSplitTick,
                                 note->durationTicks(), note->velocity());
                toRemove.append(note);
            } else if (noteEnd > relSplitTick) {
                // ノートが分割点をまたいでいる → 双方に分割
                qint64 firstHalfDuration = relSplitTick - noteStart;
                qint64 secondHalfDuration = noteEnd - relSplitTick;

                // 前半クリップ: 長さをトリム
                note->setDurationTicks(firstHalfDuration);

                // 後半クリップ: 新ノートを0開始で作成
                newClip->addNote(note->pitch(), 0, secondHalfDuration, note->velocity());
            }
            // noteEnd <= relSplitTick → ノート全体が前半、そのまま残す
        }
        for (Note* note : toRemove) {
            clip->removeNote(note);
        }
    }

    // 前半クリップの長さを短縮
    clip->setDurationTicks(relSplitTick);

    // 新クリップにPopInアニメーション
    startClipAnim(newClip->id(), ClipAnim::PopIn);
}

void ArrangementGridWidget::startSlashAnim(const QPointF& center, float length, const QColor& trackColor)
{
    SlashAnim slash;
    slash.center  = center;
    slash.startMs = m_animClock.elapsed();
    slash.progress = 0.0f;
    // 真上→真下の垂直方向の斬撃
    slash.angle   = static_cast<float>(M_PI / 2.0);  // 90°（真下方向）
    slash.length  = length * 1.2f;
    slash.color   = trackColor.lighter(160);
    m_slashAnims.append(slash);

    if (!m_animTimer.isActive()) {
        m_animTimer.start();
    }
}

void ArrangementGridWidget::drawSlashEffects(QPainter& p)
{
    if (m_slashAnims.isEmpty()) return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    for (const SlashAnim& slash : m_slashAnims) {
        float t = slash.progress;

        // Phase 1 (0〜0.3): 刃の出現 — 上から下へ切り裂く
        // Phase 2 (0.3〜1.0): 残光フェードアウト
        float slashT = qBound(0.0f, t / 0.3f, 1.0f);     // 刀の進行（0→1）
        float fadeT = qBound(0.0f, (t - 0.2f) / 0.8f, 1.0f); // フェードアウト

        float halfLen = slash.length * 0.5f;

        // 始点（上端）→ 終点（下端）を slashT で制御（真上から真下へ）
        QPointF p1(slash.center.x(), slash.center.y() - halfLen);
        QPointF p2(slash.center.x(),
                   slash.center.y() - halfLen + slash.length * slashT);

        // メインの斬撃線
        float mainAlpha = (1.0f - BurstAnimation::easeOutCubic(fadeT)) * 0.95f;
        if (mainAlpha > 0.01f) {
            // グロー（太い半透明の光）
            QColor glowColor = slash.color;
            glowColor.setAlphaF(static_cast<double>(mainAlpha * 0.3f));
            p.setPen(QPen(glowColor, 12.0f, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(p1, p2);

            // コア（細い白い線）
            QColor coreColor(255, 255, 255, static_cast<int>(mainAlpha * 255));
            p.setPen(QPen(coreColor, 3.0f, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(p1, p2);

            // エッジ（色付きの中間線）
            QColor edgeColor = slash.color;
            edgeColor.setAlphaF(static_cast<double>(mainAlpha * 0.7f));
            p.setPen(QPen(edgeColor, 5.0f, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(p1, p2);
        }

        // 衝撃波の縦線（分割位置にフラッシュ）
        if (t < 0.4f) {
            float flashAlpha = (1.0f - t / 0.4f) * 0.5f;
            QColor flashColor(255, 255, 255, static_cast<int>(flashAlpha * 255));
            float flashX = static_cast<float>(slash.center.x());
            float expand = t * 80.0f;
            p.setPen(QPen(flashColor, 2.0f));
            p.drawLine(QPointF(flashX, slash.center.y() - halfLen - expand),
                       QPointF(flashX, slash.center.y() + halfLen + expand));
        }

        // 火花パーティクル（スパーク）
        if (slashT > 0.1f && mainAlpha > 0.05f) {
            int sparkCount = 6;
            p.setPen(Qt::NoPen);
            for (int i = 0; i < sparkCount; ++i) {
                float sparkT = qBound(0.0f, (t - 0.05f * i) / 0.5f, 1.0f);
                if (sparkT <= 0.0f || sparkT >= 1.0f) continue;

                float sparkAlpha = (1.0f - sparkT) * mainAlpha;
                // 火花は左右に散らす（水平方向）
                float angle = static_cast<float>(M_PI) * (static_cast<float>(i) / sparkCount - 0.5f);
                float dist = sparkT * 30.0f + i * 3.0f;

                QPointF sparkPos(slash.center.x() + dist * qCos(angle),
                                 slash.center.y() + dist * 0.3f * qSin(angle));
                float sparkSize = (1.0f - sparkT) * 3.0f;

                QColor sparkColor = slash.color.lighter(140);
                sparkColor.setAlphaF(static_cast<double>(sparkAlpha));
                p.setBrush(sparkColor);
                p.drawEllipse(sparkPos, static_cast<double>(sparkSize), static_cast<double>(sparkSize));
            }
        }
    }

    p.restore();
}
