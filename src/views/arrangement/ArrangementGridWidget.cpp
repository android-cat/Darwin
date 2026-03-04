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
    
    auto setupTrack = [this](Track* track) {
        auto connectClip = [this](Clip* clip) {
            connect(clip, &Clip::changed, this, &ArrangementGridWidget::scheduleUpdate);
            connect(clip, &Clip::noteAdded, this, [this](Note* note) {
                connect(note, &Note::changed, this, &ArrangementGridWidget::scheduleUpdate);
                scheduleUpdate();
            });
            for (Note* note : clip->notes()) {
                connect(note, &Note::changed, this, &ArrangementGridWidget::scheduleUpdate);
            }
            updateDynamicSize();
        };

        connect(track, &Track::clipAdded, this, [this, connectClip](Clip* clip){ 
            connectClip(clip);
            startClipAnim(clip->id(), ClipAnim::PopIn);
        });
        connect(track, &Track::clipRemoved, this, [this](Clip* clip){ 
            if (m_selectedClipId == clip->id()) m_selectedClipId = -1;
            m_selectedClipIds.removeAll(clip->id());

            // 退避してアニメーション開始
            m_fadingClips[clip->id()] = clip;
            startClipAnim(clip->id(), ClipAnim::FadeOut);
            
            updateDynamicSize(); 
        });

        for (Clip* clip : track->clips()) {
            connectClip(clip);
        }
    };

    connect(m_project, &Project::trackAdded, this, [this, setupTrack](Track* track) {
        setupTrack(track);
        invalidateVisibleTracksCache();
        updateDynamicSize();
    });
    connect(m_project, &Project::trackRemoved, this, [this](Track*){ 
        invalidateVisibleTracksCache();
        updateDynamicSize(); 
    });
    connect(m_project, &Project::exportRangeChanged, this, &ArrangementGridWidget::scheduleUpdate);
    connect(m_project, &Project::folderStructureChanged, this, [this]() { 
        invalidateVisibleTracksCache();
        updateDynamicSize(); 
    });
    
    // 既存トラックの監視
    for (int i = 0; i < m_project->trackCount(); ++i) {
        setupTrack(m_project->trackAt(i));
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
    
    qint64 maxTick = 0;
    for (int i = 0; i < m_project->trackCount(); ++i) {
        Track* track = m_project->trackAt(i);
        if (!track) continue;
        for (Clip* clip : track->clips()) {
            qint64 endTick = clip->startTick() + clip->durationTicks();
            if (endTick > maxTick) maxTick = endTick;
        }
    }
    
    // 再生ヘッド位置も考慮
    if (m_playheadPosition > maxTick) maxTick = m_playheadPosition;
    
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

QList<Track*> ArrangementGridWidget::visibleTracks() const
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
    QList<Track*> vis = visibleTracks();
    return vis.indexOf(track);
}
