#pragma once
#include <QWidget>
#include <QTimer>
#include <QMap>
#include <QPointer>
#include <QElapsedTimer>
#include "common/BurstAnimationHelper.h"

class Clip;
class Note;
class Project;
class QScrollArea;
class QUndoStack;

class PianoRollGridWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PianoRollGridWidget(QWidget *parent = nullptr);
    
    void setScrollArea(QScrollArea* scrollArea) { m_scrollArea = scrollArea; }
    void setUndoStack(QUndoStack* stack) { m_undoStack = stack; }
    
signals:
    void requestSeek(qint64 tickPosition);

public slots:
    void setPlayheadPosition(qint64 tickPosition);
    void setPlaying(bool playing);
    void setActiveClip(Clip* clip);
    void setProject(Project* project);
    
private slots:
    void onLongPressTimeout();
    
protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    QSize sizeHint() const override;
    
private:
    double pixelsPerTick() const;
    qint64 gridQuantize() const;
    /** tickをグリッドにスナップ（Project::gridSnapEnabled チェック付き） */
    qint64 snapTick(qint64 tick) const;
    int computeRequiredWidth() const;
    void updateDynamicSize();
    void ensurePlayheadVisible();
    
    qint64 m_playheadPosition;
    bool m_isPlaying = false;
    float m_trailOpacity = 0.0f;
    QPointer<Clip> m_activeClip;
    Project* m_project;
    QScrollArea* m_scrollArea = nullptr;
    QUndoStack* m_undoStack = nullptr;
    
    double m_zoomLevel;
    
    // Interaction state
    Note* m_selectedNote;
    QList<Note*> m_selectedNotes;   // 複数選択
    bool m_isDragging;
    bool m_isDraggingPlayhead = false;
    bool m_isResizing;              // 右端リサイズ
    bool m_isResizingLeft;          // 左端リサイズ
    bool m_isZooming;
    bool m_longPressActive;
    QTimer m_longPressTimer;
    QPoint m_lastMousePos;
    QPoint m_pressPos;
    
    // 範囲選択（ラバーバンド）
    bool m_isRubberBanding = false;
    QPoint m_rubberBandOrigin;
    QRect m_rubberBandRect;

    // ===== アニメーション =====
    struct NoteAnim {
        qint64 startMs;
        float progress;
        enum Type { PopIn, SelectGlow, FadeOut } type;
    };
    QMap<Note*, NoteAnim> m_noteAnims;
    QList<QPointer<Note>> m_fadingNotes;         // 削除済みだがフェードアウト中のノート
    QElapsedTimer m_animClock;
    QTimer m_animTimer;
    Note* m_prevSelectedNote;

    // バースト共通エンジン
    QList<BurstAnimation::BurstGhost> m_burstGhosts;
    QList<BurstAnimation::Particle>   m_particles;

    void startNoteAnim(Note* note, NoteAnim::Type type);
    void startBurstAnim(const QRectF& rect, const QColor& color);
    void tickAnimations();
    void drawBurstEffects(QPainter& p);
    
    // ===== 描画デバウンス =====
    bool m_updatePending = false;
    /** データ変更時の遅延 update()（同一イベントループ内で複数回呼ばれても1回だけ再描画） */
    void scheduleUpdate();

    static constexpr int MIN_BARS = 64;
};
