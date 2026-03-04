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
#include <QElapsedTimer>
#include "common/BurstAnimationHelper.h"

class Project;
class Track;
class Clip;
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
    void drawClips(QPainter& p);
    void drawFolderSummary(QPainter& p, Track* folder, int y, int rowHeight);
    void drawAudioWaveform(QPainter& p, const QRectF& clipRect, class Clip* clip,
                           const QColor& waveColor);
    void handleMidiFileDrop(const QString& filePath, const QPoint& dropPos);
    void handleAudioFileDrop(const QString& filePath, const QPoint& dropPos);
    
    /** 現在表示中の（フォルダ折りたたみで隠されていない）トラック一覧 */
    QList<Track*> visibleTracks() const;
    /** 可視トラック内でのインデックスを返す (-1 = 非表示) */
    int visibleTrackIndex(Track* track) const;
    
    qint64 m_playheadPosition;
    Project* m_project;
    
    // Interaction state
    int m_selectedClipId;
    bool m_isDragging;
    bool m_isDraggingPlayhead = false;
    bool m_isResizing;      // 右端リサイズ
    bool m_isResizingLeft;  // 左端リサイズ
    QPoint m_lastMousePos;
    
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
    QMap<int, Clip*> m_fadingClips;     // 削除済みだがフェードアウト中のクリップ
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
};
