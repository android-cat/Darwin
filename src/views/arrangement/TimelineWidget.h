#pragma once

#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QElapsedTimer>

class Project;
class Track;
class Clip;
class Note;
class PlaybackController;

/**
 * @brief タイムライン（小節番号＋エクスポート範囲ハンドル＋フラッグ）ウィジェット
 *
 * ArrangementView 上部に配置される小節目盛りバー。
 * エクスポート範囲のドラッグ操作・自動スクロールに対応。
 * 小節部分の長押しでフラッグ（マーカー）を設置可能。
 */
class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumWidth(6400);
        setFixedHeight(40);
        setMouseTracking(true);
        setCursor(Qt::ArrowCursor);

        // 長押しタイマー設定（500ms）
        m_longPressTimer.setSingleShot(true);
        m_longPressTimer.setInterval(500);
        connect(&m_longPressTimer, &QTimer::timeout, this, &TimelineWidget::onLongPress);

        // フラッグアニメーションタイマー（60fps）
        m_flagAnimClock.start();
        m_flagAnimTimer.setInterval(16);
        connect(&m_flagAnimTimer, &QTimer::timeout, this, &TimelineWidget::tickFlagAnimations);
    }

    void setProject(Project* project);
    void setGridScrollArea(QScrollArea* sa) { m_gridScroll = sa; }

    /** ズーム倍率を設定（ArrangementGridWidgetと同期） */
    void setZoomLevel(double zoom) {
        if (!qFuzzyCompare(zoom, m_zoomLevel)) {
            m_zoomLevel = zoom;
            syncWidthToGrid(0); // 幅を再計算
            update();
        }
    }

signals:
    void requestSeek(qint64 tickPosition);

public slots:
    void setPlaying(bool playing);
    void syncWidthToGrid(int gridWidth) {
        if (gridWidth > minimumWidth()) {
            setMinimumWidth(gridWidth);
            updateGeometry();
            update();
        }
    }

    QSize sizeHint() const override {
        return QSize(minimumWidth(), 40);
    }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private slots:
    void onLongPress();

private:
    void autoScrollIfNeeded(double localX);
    void drawHandle(QPainter& p, int x, const QColor& color, bool isStart);
    void drawFlags(QPainter& p);

    /// コード表記更新のためのシグナル接続ヘルパー
    void connectTrackForChord(Track* track);
    void connectClipForChord(Clip* clip);

    /// コード帯のキャッシュ構造
    struct ChordSpan {
        qint64 startTick;
        qint64 endTick;
        QString name;
    };
    QList<ChordSpan> m_cachedChordSpans;
    bool m_chordCacheDirty = true;
    void rebuildChordCache();
    void invalidateChordCache() { m_chordCacheDirty = true; }

    Project* m_project = nullptr;
    QScrollArea* m_gridScroll = nullptr;
    QTimer* m_scrollTimer = nullptr;
    int m_scrollSpeed = 0;
    double m_zoomLevel = 1.0;  // ズーム倍率
    bool m_isPlaying = false;
    float m_trailOpacity = 0.0f; // 軌跡の不透明度 (0.0 - 1.0)
    enum DragMode { DragNone, DragStart, DragEnd, DragFlag, DragPlayhead };
    DragMode m_dragging = DragNone;

    // フラッグ長押し用
    QTimer m_longPressTimer;
    QPointF m_longPressPos;     // 長押し開始位置
    bool m_longPressActive = false;
    bool m_longPressFired = false;  // 長押しが成立したか

    // フラッグドラッグ移動用
    qint64 m_dragFlagOrigTick = -1;  // ドラッグ中の元のフラッグ位置
    qint64 m_dragFlagCurrTick = -1;  // ドラッグ中の現在位置

    // フラッグ配置アニメーション
    struct FlagPlantAnim {
        qint64 flagTick;   // 対象フラッグのTick位置
        qint64 startMs;    // アニメ開始時刻
        float progress;    // 0.0～1.0
    };
    QList<FlagPlantAnim> m_flagPlantAnims;
    QElapsedTimer m_flagAnimClock;
    QTimer m_flagAnimTimer;
    void tickFlagAnimations();
    void startFlagPlantAnim(qint64 flagTick);
};
