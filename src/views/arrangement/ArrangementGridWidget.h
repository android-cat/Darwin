#pragma once

#include <QWidget>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QMap>
#include <QHash>
#include <QImage>
#include <QVector>
#include <QPointer>
#include <QElapsedTimer>
#include "common/BurstAnimationHelper.h"

class Project;
class Track;
class Clip;
class Note;
class QUndoStack;

class QScrollArea;

class ArrangementGridWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ArrangementGridWidget(QWidget *parent = nullptr);
    QSize sizeHint() const override;
    
    void setProject(Project* project);
    void setUndoStack(QUndoStack* stack) { m_undoStack = stack; }
    void setScrollArea(QScrollArea* scrollArea) { m_scrollArea = scrollArea; }
    void updateDynamicSize();

    /** ズーム倍率に基づく1小節あたりのピクセル数 */
    double pixelsPerBar() const;
    /** ズーム倍率に基づく1tickあたりのピクセル数 */
    double pixelsPerTick() const;
    /** ズーム＋表示解像度に応じたグリッド量子化単位 (tick) */
    qint64 gridQuantize() const;
    /** tickをグリッドにスナップ（Project::gridSnapEnabled チェック付き） */
    qint64 snapTick(qint64 tick) const;

signals:
    void clipSelected(Clip* clip);
    void widthChanged(int newWidth);
    void requestSeek(qint64 tickPosition);
    void zoomChanged(double zoomLevel);

public slots:
    void setPlayheadPosition(qint64 tickPosition);
    void setPlaying(bool playing);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    
private:
    struct FolderSummaryCacheEntry;

    void drawClips(QPainter& p, const QRect& visibleRect);
    void drawFolderSummary(QPainter& p, Track* folder, int y, int rowHeight,
                           const QRect& visibleRect);
    void drawAudioWaveform(QPainter& p, const QRectF& clipRect, class Clip* clip,
                           const QColor& waveColor);
    void handleMidiFileDrop(const QString& filePath, const QPoint& dropPos);
    void handleAudioFileDrop(const QString& filePath, const QPoint& dropPos);
    
    /** 現在表示中の（フォルダ折りたたみで隠されていない）トラック一覧 */
    const QList<Track*>& visibleTracks() const;
    /** 可視トラック内でのインデックスを返す (-1 = 非表示) */
    int visibleTrackIndex(Track* track) const;
    /** 全クリップの末端tick。再生ヘッド更新ごとの全走査を避けるためにキャッシュする。 */
    qint64 maxContentTick() const;
    /** 右方向への拡張だけならインクリメンタルに更新し、全再計算を避ける。 */
    bool updateContentWidthCacheForClip(Clip* clip);
    void removeClipFromContentWidthCache(Clip* clip);
    const FolderSummaryCacheEntry* folderSummaryCacheForTrack(Track* folder) const;
    void rebuildFolderSummaryCache() const;
    
    /** 可視トラックキャッシュ（フォルダ構造変更・トラック追加削除時にのみ再構築） */
    mutable QList<Track*> m_cachedVisibleTracks;
    mutable bool m_visibleTracksCacheDirty = true;
    void invalidateVisibleTracksCache() { m_visibleTracksCacheDirty = true; }
    mutable QHash<int, qint64> m_clipEndTickCache;
    mutable qint64 m_cachedMaxContentTick = 0;
    mutable bool m_contentWidthCacheDirty = true;
    void invalidateContentWidthCache() { m_contentWidthCacheDirty = true; }
    struct WaveformCacheEntry {
        int width = 0;
        int height = 0;
        QRgb color = 0;
        QImage image;
    };
    // 波形はズーム中に同じ見た目で何度も描かれるので、最後に使った画像をクリップ単位で保持する。
    mutable QHash<int, WaveformCacheEntry> m_waveformCache;
    struct FolderSummaryNoteEntry {
        qint64 startTick = 0;
        qint64 durationTicks = 0;
        int pitch = 0;
        QColor color;
    };
    struct FolderSummaryClipEntry {
        qint64 startTick = 0;
        qint64 endTick = 0;
        QVector<FolderSummaryNoteEntry> notes;
    };
    struct FolderSummaryCacheEntry {
        bool hasAnyClip = false;
        qint64 minTick = 0;
        qint64 maxTick = 0;
        QVector<FolderSummaryClipEntry> clips;
    };
    // フォルダ要約は子孫探索とクリップ集計が重いため、描画用データをフォルダ単位で保持する。
    mutable QHash<int, FolderSummaryCacheEntry> m_folderSummaryCache;
    mutable bool m_folderSummaryCacheDirty = true;
    void invalidateFolderSummaryCache()
    {
        m_folderSummaryCacheDirty = true;
        m_folderSummaryCache.clear();
    }
    
    qint64 m_playheadPosition;
    bool m_isPlaying = false;
    float m_trailOpacity = 0.0f; // 軌跡の不透明度 (0.0 - 1.0)
    Project* m_project;
    
    // Interaction state
    int m_selectedClipId;
    bool m_isDragging;
    bool m_isDraggingPlayhead = false;
    bool m_isResizing;      // 右端リサイズ
    bool m_isResizingLeft;  // 左端リサイズ
    QPoint m_lastMousePos;

    // ドラッグ/リサイズ開始時の元位置（Undo用）
    struct ClipOrigState { qint64 startTick; qint64 durationTicks; };
    QHash<int, ClipOrigState> m_clipOrigStates;  // clipId → 元の状態

    // リサイズ時のノート元状態（Undo用）
    struct NoteOrigState { qint64 startTick; qint64 durationTicks; };
    QHash<Note*, NoteOrigState> m_resizeNoteOrigStates;
    QList<Note*> m_resizeRemovedNotes;

    // クリップのトラック間移動
    int m_dragSourceTrackIndex = -1;
    int m_dragCurrentTrackIndex = -1;

    // 範囲選択（ラバーバンド）
    bool m_isRubberBanding = false;
    QPoint m_rubberBandOrigin;
    QRect m_rubberBandRect;
    QList<int> m_selectedClipIds;  // 複数選択クリップID

    // ===== アニメーション =====
    struct ClipAnim {
        qint64 startMs;
        float progress;
        enum Type { PopIn, FadeOut, SelectPulse, BurstOut } type;
    };
    QMap<int, ClipAnim> m_clipAnims;
    QMap<int, QPointer<Clip>> m_fadingClips;     // 削除済みだがフェードアウト中のクリップ
    QElapsedTimer m_animClock;
    QTimer m_animTimer;
    int m_prevSelectedClipId;

    // バースト共通エンジン
    QList<BurstAnimation::BurstGhost> m_burstGhosts;
    QList<BurstAnimation::Particle>   m_particles;

    // ===== 長押し分割（斬撃エフェクト） =====
    QTimer m_longPressTimer;
    QPoint m_longPressPos;         // 長押し開始位置（ウィジェット座標）
    int    m_longPressClipId = -1; // 長押し対象クリップID
    int    m_longPressTrackIdx = -1; // 長押し対象トラックの可視インデックス

    /** 長押し確定時の処理（分割＋斬撃アニメーション起動） */
    void onLongPressConfirmed();
    /** クリップを指定tick位置で分割する。ノートがまたがる場合は双方に分割して配置 */
    void splitClipAt(Track* track, Clip* clip, qint64 splitTick);

    // 斬撃アニメーション
    struct SlashAnim {
        QPointF center;      // 斬撃の中心座標
        qint64  startMs;
        float   progress;    // 0.0〜1.0
        float   angle;       // 斜線の角度（rad）
        float   length;      // 斬撃の長さ（px）
        QColor  color;       // 斬撃の光の色
    };
    QList<SlashAnim> m_slashAnims;

    void startSlashAnim(const QPointF& center, float length, const QColor& trackColor);
    void drawSlashEffects(QPainter& p);

    // ===== MIDIドロップ波リビールアニメーション =====
    struct WaveReveal {
        int     clipId;        // 対象クリップID
        QPointF dropPoint;     // ドロップ地点（ウィジェット座標）
        qint64  startMs;       // 開始時刻
        float   progress;      // 0.0〜1.0（展開率: spread / maxDist）
        float   maxDist;       // ドロップ地点からクリップ端までの最大距離(px)
    };

    // 速度ベースの波展開距離を計算（クリップサイズに依存しない視覚的ペース）
    static float computeWaveSpread(float elapsedMs);
    QList<WaveReveal> m_waveReveals;

    void startClipAnim(int clipId, ClipAnim::Type type);
    void startBurstAnim(const QRectF& rect, const QColor& color, int trackIndex);
    void startWaveReveal(int clipId, const QPointF& dropPoint);
    void tickAnimations();
    void drawBurstEffects(QPainter& p);
    void drawWaveRevealClip(QPainter& p, const WaveReveal& wave);

    int computeRequiredWidth() const;
    void ensurePlayheadVisible();

    QScrollArea* m_scrollArea = nullptr;
    QUndoStack* m_undoStack = nullptr;
    double m_zoomLevel = 1.0;   // ズーム倍率 (0.25x 〜 4.0x)
    static constexpr int MIN_BARS = 64;

    // ===== 描画デバウンス =====
    bool m_updatePending = false;
    /** データ変更時の遅延 update()（同一イベントループ内で複数回呼ばれても1回だけ再描画） */
    void scheduleUpdate();
};
