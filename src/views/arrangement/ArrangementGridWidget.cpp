/**
 * @file ArrangementGridWidget.cpp
 * @brief ArrangementGridWidget のコンストラクタ、セットアップ、サイズ/レイアウト、ユーティリティ
 *
 * 描画・入力・D&D・アニメーション処理はそれぞれ別ファイルに配置:
 *   - ArrangementGridWidget_Painting.cpp
 *   - ArrangementGridWidget_Input.cpp
 *   - ArrangementGridWidget_DragDrop.cpp
 *   - ArrangementGridWidget_Animation.cpp
 */
#include "ArrangementGridWidget.h"
#include "models/Project.h"
#include "models/Track.h"
#include "models/Clip.h"
#include "models/Note.h"
#include <limits>
#include <QScrollArea>
#include <QScrollBar>
#include "common/Constants.h"
#include "common/BurstAnimationHelper.h"

using namespace Darwin;

ArrangementGridWidget::ArrangementGridWidget(QWidget *parent) 
    : QWidget(parent)
    , m_playheadPosition(0)
    , m_project(nullptr)
    , m_selectedClipId(-1)
    , m_isDragging(false)
    , m_isResizing(false)
    , m_isResizingLeft(false)
    , m_prevSelectedClipId(-1)
    , m_zoomLevel(1.0)
    , m_updatePending(false)
{
    setMinimumWidth(static_cast<int>(MIN_BARS * pixelsPerBar())); // 最低4小節
    setMouseTracking(true); // For resize cursor
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true); // MIDIファイルのドラッグ&ドロップを有効化

    // アニメーションタイマー（60fps）
    m_animClock.start();
    connect(&m_animTimer, &QTimer::timeout, this, &ArrangementGridWidget::tickAnimations);
    m_animTimer.setInterval(16);

    // 長押し分割タイマー（500ms で確定）
    m_longPressTimer.setSingleShot(true);
    m_longPressTimer.setInterval(500);
    connect(&m_longPressTimer, &QTimer::timeout, this, &ArrangementGridWidget::onLongPressConfirmed);
}

void ArrangementGridWidget::setProject(Project* project)
{
    m_project = project;
    invalidateVisibleTracksCache();
    m_clipEndTickCache.clear();
    m_cachedMaxContentTick = 0;
    invalidateContentWidthCache();
    m_waveformCache.clear();
    invalidateFolderSummaryCache();
    
    auto setupTrack = [this](Track* track) {
        auto connectClip = [this](Clip* clip) {
            connect(clip, &Clip::changed, this, [this, clip]() {
                m_waveformCache.remove(clip->id());
                invalidateFolderSummaryCache();
                if (updateContentWidthCacheForClip(clip)) {
                    updateDynamicSize();
                } else {
                    scheduleUpdate();
                }
            });
            connect(clip, &Clip::noteAdded, this, [this](Note* note) {
                connect(note, &Note::changed, this, &ArrangementGridWidget::scheduleUpdate);
                scheduleUpdate();
            });
            for (Note* note : clip->notes()) {
                connect(note, &Note::changed, this, &ArrangementGridWidget::scheduleUpdate);
            }
        };

        connect(track, &Track::propertyChanged, this, [this]() {
            // トラック色や表示状態が変わると要約描画の見た目とレイアウトが変わるため、関連キャッシュを更新する。
            invalidateVisibleTracksCache();
            invalidateFolderSummaryCache();
            updateDynamicSize();
        });
        connect(track, &Track::clipAdded, this, [this, connectClip](Clip* clip){ 
            connectClip(clip);
            m_waveformCache.remove(clip->id());
            invalidateFolderSummaryCache();
            updateContentWidthCacheForClip(clip);
            startClipAnim(clip->id(), ClipAnim::PopIn);
            updateDynamicSize();
        });
        connect(track, &Track::clipRemoved, this, [this](Clip* clip){ 
            if (m_selectedClipId == clip->id()) m_selectedClipId = -1;
            m_selectedClipIds.removeAll(clip->id());

            // 退避してアニメーション開始
            m_fadingClips[clip->id()] = clip;
            m_waveformCache.remove(clip->id());
            invalidateFolderSummaryCache();
            removeClipFromContentWidthCache(clip);
            startClipAnim(clip->id(), ClipAnim::FadeOut);
            
            updateDynamicSize(); 
        });

        for (Clip* clip : track->clips()) {
            connectClip(clip);
        }
    };

    if (m_project) {
        connect(m_project, &Project::trackAdded, this, [this, setupTrack](Track* track) {
            setupTrack(track);
            invalidateVisibleTracksCache();
            invalidateContentWidthCache();
            invalidateFolderSummaryCache();
            updateDynamicSize();
        });
        connect(m_project, &Project::trackRemoved, this, [this](Track*){ 
            invalidateVisibleTracksCache();
            invalidateContentWidthCache();
            invalidateFolderSummaryCache();
            updateDynamicSize(); 
        });
        connect(m_project, &Project::trackOrderChanged, this, [this]() {
            // トラック順の変更後に古い並び順を使わないよう、可視トラックキャッシュを更新する。
            invalidateVisibleTracksCache();
            updateDynamicSize();
        });
        connect(m_project, &Project::exportRangeChanged, this, &ArrangementGridWidget::scheduleUpdate);
        connect(m_project, &Project::folderStructureChanged, this, [this]() { 
            invalidateVisibleTracksCache();
            invalidateFolderSummaryCache();
            updateDynamicSize(); 
        });
        
        // 既存トラックの監視
        for (int i = 0; i < m_project->trackCount(); ++i) {
            setupTrack(m_project->trackAt(i));
        }
    }
    updateDynamicSize();
}

void ArrangementGridWidget::scheduleUpdate()
{
    if (m_updatePending) return;
    m_updatePending = true;
    QTimer::singleShot(0, this, [this]() {
        m_updatePending = false;
        update();
    });
}

double ArrangementGridWidget::pixelsPerBar() const
{
    return PIXELS_PER_BAR * m_zoomLevel;
}

double ArrangementGridWidget::pixelsPerTick() const
{
    return PIXELS_PER_TICK * m_zoomLevel;
}

qint64 ArrangementGridWidget::gridQuantize() const
{
    double ppt = pixelsPerTick();
    double beatWidth = TICKS_PER_BEAT * ppt;

    if (beatWidth >= 200) return TICKS_PER_BEAT / 8; // 32分音符 (60 ticks)
    if (beatWidth >= 100) return TICKS_PER_BEAT / 4; // 16分音符 (120 ticks)
    if (beatWidth >= 50)  return TICKS_PER_BEAT / 2; // 8分音符 (240 ticks)
    return TICKS_PER_BEAT;                            // 4分音符 (480 ticks)
}

qint64 ArrangementGridWidget::snapTick(qint64 tick) const
{
    if (!m_project || !m_project->gridSnapEnabled()) return tick;
    qint64 q = gridQuantize();
    if (q <= 0) return tick;
    // 最も近いグリッド線にスナップ (四捨五入)
    return ((tick + q / 2) / q) * q;
}

QSize ArrangementGridWidget::sizeHint() const
{
    int h = 0;
    if (m_project && m_project->trackCount() > 0) {
        h = visibleTracks().size() * 100;
    }
    
    return QSize(computeRequiredWidth(), h);
}

int ArrangementGridWidget::computeRequiredWidth() const
{
    int minWidth = static_cast<int>(MIN_BARS * pixelsPerBar());
    if (!m_project) return minWidth;

    qint64 maxTick = qMax(maxContentTick(), m_playheadPosition);
    
    // コンテンツ末端＋余白8小節分
    int contentWidth = static_cast<int>(maxTick * pixelsPerTick()) + static_cast<int>(8 * pixelsPerBar());
    return qMax(minWidth, contentWidth);
}

void ArrangementGridWidget::updateDynamicSize()
{
    if (m_project) {
        setMinimumHeight(visibleTracks().size() * 100);
    }
    int newWidth = computeRequiredWidth();
    if (newWidth != minimumWidth()) {
        setMinimumWidth(newWidth);
        emit widthChanged(newWidth);
    }
    updateGeometry();
    update();
}

void ArrangementGridWidget::ensurePlayheadVisible()
{
    if (!m_scrollArea) return;
    
    int playheadX = static_cast<int>(m_playheadPosition * pixelsPerTick());
    QScrollBar* hBar = m_scrollArea->horizontalScrollBar();
    if (!hBar) return;
    
    int viewportWidth = m_scrollArea->viewport()->width();
    int scrollPos = hBar->value();
    
    // 再生ヘッドがビューポートの右端80%を超えたら、その分だけスクロールして追従
    int rightThreshold = scrollPos + static_cast<int>(viewportWidth * 0.8);
    // 再生ヘッドがビューポートの左端20%より前に出たら、それに合わせてスクロール
    int leftThreshold = scrollPos + static_cast<int>(viewportWidth * 0.2);
    
    if (playheadX > rightThreshold) {
        hBar->setValue(playheadX - static_cast<int>(viewportWidth * 0.8));
    } else if (playheadX < leftThreshold) {
        hBar->setValue(playheadX - static_cast<int>(viewportWidth * 0.2));
    }
}

void ArrangementGridWidget::setPlaying(bool playing)
{
    m_isPlaying = playing;
    if (!m_animTimer.isActive()) {
        m_animTimer.start();
    }
    update();
}

void ArrangementGridWidget::setPlayheadPosition(qint64 tickPosition)
{
    m_playheadPosition = tickPosition;
    
    // コンテンツを動的に拡張（再生位置が端に近づいた場合）
    int newWidth = computeRequiredWidth();
    if (newWidth > minimumWidth()) {
        setMinimumWidth(newWidth);
        emit widthChanged(newWidth);
        updateGeometry();
    }
    
    // 再生ヘッドにスクロール追従
    ensurePlayheadVisible();
    
    update();
}

const QList<Track*>& ArrangementGridWidget::visibleTracks() const
{
    if (!m_visibleTracksCacheDirty) {
        return m_cachedVisibleTracks;
    }
    m_cachedVisibleTracks.clear();
    if (!m_project) return m_cachedVisibleTracks;
    for (int i = 0; i < m_project->trackCount(); ++i) {
        Track* track = m_project->trackAt(i);
        if (m_project->isTrackVisibleInHierarchy(track)) {
            m_cachedVisibleTracks.append(track);
        }
    }
    m_visibleTracksCacheDirty = false;
    return m_cachedVisibleTracks;
}

int ArrangementGridWidget::visibleTrackIndex(Track* track) const
{
    const QList<Track*>& vis = visibleTracks();
    return vis.indexOf(track);
}

qint64 ArrangementGridWidget::maxContentTick() const
{
    if (!m_contentWidthCacheDirty) {
        return m_cachedMaxContentTick;
    }

    // 縮小や削除が入った時だけ全体を再走査し、通常の再生中は結果を使い回す。
    m_cachedMaxContentTick = 0;
    m_clipEndTickCache.clear();

    if (m_project) {
        for (int i = 0; i < m_project->trackCount(); ++i) {
            Track* track = m_project->trackAt(i);
            if (!track) {
                continue;
            }

            for (Clip* clip : track->clips()) {
                const qint64 endTick = clip->endTick();
                m_clipEndTickCache.insert(clip->id(), endTick);
                if (endTick > m_cachedMaxContentTick) {
                    m_cachedMaxContentTick = endTick;
                }
            }
        }
    }

    m_contentWidthCacheDirty = false;
    return m_cachedMaxContentTick;
}

bool ArrangementGridWidget::updateContentWidthCacheForClip(Clip* clip)
{
    if (!clip) {
        return false;
    }

    const qint64 newEndTick = clip->endTick();
    const qint64 oldEndTick = m_clipEndTickCache.value(clip->id(), newEndTick);
    m_clipEndTickCache.insert(clip->id(), newEndTick);

    // 右方向への拡張はその場で最大値を更新できる。
    if (newEndTick > m_cachedMaxContentTick) {
        m_cachedMaxContentTick = newEndTick;
        return true;
    }

    // 最大値だったクリップが短くなった場合だけ、次回に全体再計算する。
    if (oldEndTick == m_cachedMaxContentTick && newEndTick < oldEndTick) {
        m_contentWidthCacheDirty = true;
    }

    return false;
}

void ArrangementGridWidget::removeClipFromContentWidthCache(Clip* clip)
{
    if (!clip) {
        return;
    }

    const qint64 removedEndTick = m_clipEndTickCache.take(clip->id());
    if (removedEndTick >= m_cachedMaxContentTick) {
        m_contentWidthCacheDirty = true;
    }
}

const ArrangementGridWidget::FolderSummaryCacheEntry*
ArrangementGridWidget::folderSummaryCacheForTrack(Track* folder) const
{
    if (!folder || !folder->isFolder()) {
        return nullptr;
    }

    if (m_folderSummaryCacheDirty) {
        rebuildFolderSummaryCache();
    }

    auto it = m_folderSummaryCache.constFind(folder->id());
    return (it != m_folderSummaryCache.constEnd()) ? &it.value() : nullptr;
}

void ArrangementGridWidget::rebuildFolderSummaryCache() const
{
    m_folderSummaryCache.clear();

    if (!m_project) {
        m_folderSummaryCacheDirty = false;
        return;
    }

    // フォルダごとの子孫・クリップ範囲・ノート色を一度だけ集計し、paintEvent では描画に専念する。
    for (Track* track : m_project->tracks()) {
        if (!track || !track->isFolder()) {
            continue;
        }

        FolderSummaryCacheEntry cacheEntry;
        cacheEntry.minTick = std::numeric_limits<qint64>::max();

        const QList<Track*> children = m_project->folderDescendants(track);
        for (Track* child : children) {
            QColor noteColor = child->color().darker(110);
            noteColor.setAlpha(180);

            for (Clip* clip : child->clips()) {
                FolderSummaryClipEntry clipEntry;
                clipEntry.startTick = clip->startTick();
                clipEntry.endTick = clip->endTick();

                cacheEntry.hasAnyClip = true;
                cacheEntry.minTick = qMin(cacheEntry.minTick, clipEntry.startTick);
                cacheEntry.maxTick = qMax(cacheEntry.maxTick, clipEntry.endTick);

                clipEntry.notes.reserve(clip->notes().size());
                for (Note* note : clip->notes()) {
                    FolderSummaryNoteEntry noteEntry;
                    noteEntry.startTick = clip->startTick() + note->startTick();
                    noteEntry.durationTicks = note->durationTicks();
                    noteEntry.pitch = note->pitch();
                    noteEntry.color = noteColor;
                    clipEntry.notes.push_back(noteEntry);
                }

                cacheEntry.clips.push_back(clipEntry);
            }
        }

        if (cacheEntry.hasAnyClip) {
            m_folderSummaryCache.insert(track->id(), cacheEntry);
        }
    }

    m_folderSummaryCacheDirty = false;
}
