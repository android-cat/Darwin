#include "TimelineWidget.h"
#include "models/Project.h"
#include "models/Track.h"
#include "models/Clip.h"
#include "models/Note.h"
#include "common/Constants.h"
#include "common/ChordDetector.h"
#include "common/ThemeManager.h"

void TimelineWidget::setProject(Project* project)
{
    m_project = project;
    m_chordCacheDirty = true;
    if (m_project) {
        connect(m_project, &Project::exportRangeChanged, this, QOverload<>::of(&QWidget::update));
        connect(m_project, &Project::flagsChanged, this, QOverload<>::of(&QWidget::update));
        connect(m_project, &Project::playheadChanged, this, [this](qint64){ update(); });

        // 新トラック追加時
        connect(m_project, &Project::trackAdded, this, &TimelineWidget::connectTrackForChord);

        // 既存トラックを接続
        for (Track* track : m_project->tracks()) {
            connectTrackForChord(track);
        }
    }
    update();
}

void TimelineWidget::setPlaying(bool playing)
{
    m_isPlaying = playing;
    if (!m_flagAnimTimer.isActive()) {
        m_flagAnimTimer.start();
    }
    update();
}

/**
 * @brief トラックの変更をコード表記更新に接続する
 */
void TimelineWidget::connectTrackForChord(Track* track)
{
    connect(track, &Track::propertyChanged, this, [this]() {
        invalidateChordCache();
        update();
    });

    connect(track, &Track::clipAdded, this, [this](Clip* clip) {
        connectClipForChord(clip);
        invalidateChordCache();
        update();
    });

    connect(track, &Track::clipRemoved, this, [this]() {
        invalidateChordCache();
        update();
    });

    for (Clip* clip : track->clips()) {
        connectClipForChord(clip);
    }
}

/**
 * @brief クリップおよびその中のノート変更をコード表記更新に接続する
 */
void TimelineWidget::connectClipForChord(Clip* clip)
{
    connect(clip, &Clip::changed, this, [this]() {
        invalidateChordCache();
        update();
    });

    connect(clip, &Clip::noteAdded, this, [this](Note* note) {
        connect(note, &Note::changed, this, [this]() {
            invalidateChordCache();
            update();
        });
        invalidateChordCache();
        update();
    });

    for (Note* note : clip->notes()) {
        connect(note, &Note::changed, this, [this]() {
            invalidateChordCache();
            update();
        });
    }
}

void TimelineWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), Darwin::ThemeManager::instance().panelBackgroundColor());

    const double barWidth = Darwin::PIXELS_PER_BAR * m_zoomLevel;
    int numBars = qMax(1600, width()) / qMax(1, static_cast<int>(barWidth));

    // ── エクスポート範囲の背景 ──
    if (m_project) {
        double startBar = m_project->exportStartBar();
        double endBar = m_project->exportEndBar();
        if (startBar >= 0 && endBar > startBar) {
            double x1 = startBar * barWidth;
            double x2 = endBar * barWidth;
            p.fillRect(QRectF(0, 0, x1, height()), QColor(0, 0, 0, 18));
            p.fillRect(QRectF(x2, 0, width() - x2, height()), QColor(0, 0, 0, 18));
            p.fillRect(QRectF(x1, 0, x2 - x1, height()), QColor(59, 130, 246, 25));
        }
    }

    // ── 小節番号 ──
    p.setPen(QColor("#94a3b8"));
    QFont f = p.font();
    f.setPixelSize(10);
    f.setBold(true);
    p.setFont(f);

    // 小節番号テキストと目盛り線
    for (int i = 0; i <= numBars; ++i) {
        double x = i * barWidth;
        p.setPen(QColor("#94a3b8"));
        p.drawLine(QPointF(x, 14), QPointF(x, 20));
        p.drawText(QPointF(x + 4, 12), QString::number(i + 1));
    }

    // ── エクスポート範囲ハンドル ──
    if (m_project) {
        double startBar = m_project->exportStartBar();
        double endBar = m_project->exportEndBar();
        if (startBar >= 0 && endBar > startBar) {
            double sx = startBar * barWidth;
            double ex = endBar * barWidth;
            // drawHandle internally uses int currently, we might need to change it later if we want float
            drawHandle(p, static_cast<int>(sx), QColor(34, 197, 94), true);
            drawHandle(p, static_cast<int>(ex), QColor(239, 68, 68), false);
            p.fillRect(QRectF(sx, 0, ex - sx, 3), QColor(59, 130, 246, 180));
        }
    }

    // ===== 小節下部のコード帯（ノート実範囲ベース） =====
    if (m_project) {
        using namespace Darwin;

        // コード帯の描画領域（小節番号行の下）
        const int chordLaneTop = 22;
        const int chordLaneH   = 16;

        // 分割線
        p.setPen(Darwin::ThemeManager::instance().borderColor());
        p.drawLine(0, chordLaneTop, width(), chordLaneTop);

        // コード帯の背景
        p.fillRect(0, chordLaneTop, width(), chordLaneH + 2, Darwin::ThemeManager::instance().backgroundColor());

        // ── キャッシュされたコード帯を描画 ──
        //   ノート変更時にのみ再計算し、paintEvent毎の全ノート走査を回避
        if (m_chordCacheDirty) {
            rebuildChordCache();
        }

        if (!m_cachedChordSpans.isEmpty()) {
            p.setRenderHint(QPainter::Antialiasing, true);
            QFont chordFont("Segoe UI", 8);
            chordFont.setBold(true);
            p.setFont(chordFont);
            QFontMetrics cfm(chordFont);

            for (const auto& span : m_cachedChordSpans) {
                double x1 = span.startTick * PIXELS_PER_TICK * m_zoomLevel;
                double x2 = span.endTick   * PIXELS_PER_TICK * m_zoomLevel;
                double spanW = x2 - x1;
                if (spanW < 4.0) continue; // 極小区間はスキップ

                QRectF spanRect(x1 + 1, chordLaneTop + 1, spanW - 2, chordLaneH - 2);

                // コード帯ブロック（アクセント色の薄い背景 + 左ボーダー）
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 51, 102, 18));  // #FF3366 @ 7%
                p.drawRoundedRect(spanRect, 3, 3);

                // 左端にアクセントライン
                p.setPen(QPen(QColor("#FF3366"), 2));
                p.drawLine(QPointF(x1 + 1, chordLaneTop + 2), QPointF(x1 + 1, chordLaneTop + chordLaneH - 2));

                // コード名テキスト（幅が十分ある場合のみ）
                if (spanW > 12.0) {
                    p.setPen(Darwin::ThemeManager::instance().secondaryTextColor());  // ダーク: スレートグレー
                    double maxTextW = spanW - 8.0;
                    QString elidedName = cfm.elidedText(span.name, Qt::ElideRight, static_cast<int>(maxTextW));
                    p.drawText(QRectF(x1 + 6, chordLaneTop + 2, maxTextW, chordLaneH - 4),
                               Qt::AlignLeft | Qt::AlignVCenter, elidedName);
                }
            }

            p.setRenderHint(QPainter::Antialiasing, false);
        }
    }

    // ===== フラッグ（マーカー）の描画 =====
    drawFlags(p);

    // ── プレイヘッド位置 ──
    if (m_project) {
        double playheadX = m_project->playheadPosition() * Darwin::PIXELS_PER_TICK * m_zoomLevel;
        p.setRenderHint(QPainter::Antialiasing, true);

        // 再生中の「光の軌跡（モーションブラー）」演出
        if (m_trailOpacity > 0.001f) {
            double trailLen = 60.0;
            QLinearGradient trailGradient(playheadX, 0, playheadX - trailLen, 0);
            QColor trailColor = QColor("#FF3366");
            trailColor.setAlpha(static_cast<int>(120 * m_trailOpacity));
            trailGradient.setColorAt(0, trailColor);
            trailGradient.setColorAt(1, Qt::transparent);

            p.fillRect(QRectF(playheadX - trailLen, 0, trailLen, height()), trailGradient);
            
            // 軌跡の境界（微かな光の筋）
            p.setPen(QPen(trailColor, 1));
            p.drawLine(QPointF(playheadX - 1, 0), QPointF(playheadX - 1, height()));
        }

        // メインの再生ヘッド線
        p.setPen(QPen(QColor("#FF3366"), 2));
        p.drawLine(QPointF(playheadX, 0), QPointF(playheadX, height()));
        
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    // Bottom border
    p.setPen(Darwin::ThemeManager::instance().borderColor());
    p.drawLine(0, height() - 1, width(), height() - 1);
}

void TimelineWidget::mousePressEvent(QMouseEvent* event)
{
    if (!m_project) {
        QWidget::mousePressEvent(event);
        return;
    }

    double mx = event->position().x();

    // 右クリック: フラッグ削除判定
    if (event->button() == Qt::RightButton) {
        // クリック位置に近いフラッグを探して削除
        using namespace Darwin;
        const auto& flags = m_project->flags();
        for (qint64 flagTick : flags) {
            int fx = static_cast<int>(flagTick * PIXELS_PER_TICK * m_zoomLevel);
            if (qAbs(mx - fx) < 10) {
                m_project->removeFlag(flagTick);
                return;
            }
        }
        QWidget::mousePressEvent(event);
        return;
    }

    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const double barWidth = Darwin::PIXELS_PER_BAR * m_zoomLevel;
    double startBar = m_project->exportStartBar();
    double endBar = m_project->exportEndBar();

    if (startBar >= 0 && endBar > startBar) {
        double sx = startBar * barWidth;
        double ex = endBar * barWidth;
        if (qAbs(mx - sx) < 8) {
            m_dragging = DragStart;
            m_longPressTimer.stop();
            m_longPressActive = false;
            return;
        }
        if (qAbs(mx - ex) < 8) {
            m_dragging = DragEnd;
            m_longPressTimer.stop();
            m_longPressActive = false;
            return;
        }
    }

    // プレイヘッド位置のドラッグ判定
    int playheadX = static_cast<int>(m_project->playheadPosition() * Darwin::PIXELS_PER_TICK * m_zoomLevel);
    if (qAbs(mx - playheadX) <= 15) {
        m_dragging = DragPlayhead;
        m_longPressTimer.stop();
        m_longPressActive = false;
        return;
    }

    // 長押しタイマーを開始（フラッグ設置用）
    m_longPressPos = event->position();
    m_longPressActive = true;
    m_longPressFired = false;
    m_longPressTimer.start();

    // エクスポート範囲はここでは設定しない（mouseReleaseEventで判定）
    m_dragging = DragNone;
    update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_project) return;
    const double barWidth = Darwin::PIXELS_PER_BAR * m_zoomLevel;
    double mx = event->position().x();
    double bar = qMax(0.0, mx / barWidth);

    // フラッグドラッグ中の移動処理
    if (m_dragging == DragFlag) {
        using namespace Darwin;
        qint64 newTick = qMax(0LL, static_cast<qint64>(mx / (PIXELS_PER_TICK * m_zoomLevel)));
        m_dragFlagCurrTick = newTick;
        autoScrollIfNeeded(mx);
        update();
        return;
    }

    // プレイヘッドドラッグ中の移動処理
    if (m_dragging == DragPlayhead) {
        using namespace Darwin;
        qint64 newTick = qMax(0LL, static_cast<qint64>(mx / (PIXELS_PER_TICK * m_zoomLevel)));
        emit requestSeek(newTick);
        autoScrollIfNeeded(mx);
        update();
        return;
    }

    // マウスが動いたら長押しをキャンセル
    if (m_longPressActive) {
        double dist = QPointF(event->position() - m_longPressPos).manhattanLength();
        if (dist > 5.0) {
            m_longPressTimer.stop();
            m_longPressActive = false;
        }
    }

    if (m_dragging == DragNone) {
        // フラッグ付近でカーソル変更
        using namespace Darwin;
        bool nearFlag = false;
        const auto& flags = m_project->flags();
        for (qint64 flagTick : flags) {
            int fx = static_cast<int>(flagTick * PIXELS_PER_TICK * m_zoomLevel);
            if (qAbs(mx - fx) < 10) {
                setCursor(Qt::OpenHandCursor);
                nearFlag = true;
                break;
            }
        }
        if (!nearFlag) {
            double startBar = m_project->exportStartBar();
            double endBar = m_project->exportEndBar();
            if (startBar >= 0 && endBar > startBar) {
                double sx = startBar * barWidth;
                double ex = endBar * barWidth;
                if (qAbs(mx - sx) < 8 || qAbs(mx - ex) < 8) {
                    setCursor(Qt::SizeHorCursor);
                } else {
                    setCursor(Qt::ArrowCursor);
                }
            } else {
                setCursor(Qt::ArrowCursor);
            }
        }
        return;
    }

    autoScrollIfNeeded(mx);

    if (m_dragging == DragStart) {
        if (bar < m_project->exportEndBar() - 0.05) {
            m_project->setExportStartBar(bar);
        }
    } else if (m_dragging == DragEnd) {
        if (bar > m_project->exportStartBar() + 0.05) {
            m_project->setExportEndBar(bar);
        }
    }
    update();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event)

    // フラッグドラッグ完了処理
    if (m_dragging == DragFlag && m_dragFlagOrigTick >= 0) {
        m_project->removeFlag(m_dragFlagOrigTick);
        if (m_dragFlagCurrTick >= 0) {
            m_project->addFlag(m_dragFlagCurrTick);
        }
        m_dragFlagOrigTick = -1;
        m_dragFlagCurrTick = -1;
        setCursor(Qt::ArrowCursor);
    }

    // 長押しが成立せず、ドラッグ中でもない → 通常クリックとしてエクスポート範囲を設定
    if (m_project && !m_longPressFired && m_dragging == DragNone && m_longPressActive) {
        const double barWidth = Darwin::PIXELS_PER_BAR * m_zoomLevel;
        double mx = m_longPressPos.x();
        double bar = qMax(0.0, mx / barWidth);
        m_project->setExportStartBar(bar);
        m_project->setExportEndBar(bar + 4.0);
        m_dragging = DragEnd;
    }

    m_dragging = DragNone;
    m_longPressTimer.stop();
    m_longPressActive = false;
    m_longPressFired = false;
    if (m_scrollTimer) {
        m_scrollTimer->stop();
        delete m_scrollTimer;
        m_scrollTimer = nullptr;
    }
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_project && event->button() == Qt::LeftButton) {
        m_project->setExportStartBar(-1.0);
        m_project->setExportEndBar(-1.0);
        update();
    }
}

void TimelineWidget::autoScrollIfNeeded(double localX)
{
    QScrollArea* sa = m_gridScroll;
    if (!sa) {
        auto* parentSA = qobject_cast<QScrollArea*>(parentWidget());
        if (parentSA) sa = parentSA;
        else return;
    }

    QScrollBar* hbar = sa->horizontalScrollBar();
    if (!hbar) return;

    int viewportWidth = sa->viewport()->width();
    int scrollVal = hbar->value();
    double viewX = localX - scrollVal;
    int scrollMargin = 40;
    int scrollSpeed = 0;

    if (viewX > viewportWidth - scrollMargin) {
        scrollSpeed = static_cast<int>(8 + (viewX - (viewportWidth - scrollMargin)) * 0.5);
    } else if (viewX < scrollMargin) {
        scrollSpeed = -static_cast<int>(8 + (scrollMargin - viewX) * 0.5);
    }

    if (scrollSpeed != 0) {
        if (!m_scrollTimer) {
            m_scrollTimer = new QTimer(this);
            m_scrollTimer->setInterval(16);
            connect(m_scrollTimer, &QTimer::timeout, this, [this, hbar]() {
                hbar->setValue(hbar->value() + m_scrollSpeed);
            });
        }
        m_scrollSpeed = scrollSpeed;
        if (!m_scrollTimer->isActive()) {
            m_scrollTimer->start();
        }
    } else {
        if (m_scrollTimer && m_scrollTimer->isActive()) {
            m_scrollTimer->stop();
        }
    }
}

void TimelineWidget::drawHandle(QPainter& p, int x, const QColor& color, bool isStart)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    if (isStart) {
        path.moveTo(x, 0);
        path.lineTo(x + 8, 0);
        path.lineTo(x, 12);
        path.closeSubpath();
    } else {
        path.moveTo(x, 0);
        path.lineTo(x - 8, 0);
        path.lineTo(x, 12);
        path.closeSubpath();
    }
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(path);
    p.setPen(QPen(color, 1.5));
    p.drawLine(x, 0, x, height());
    p.restore();
}

void TimelineWidget::drawFlags(QPainter& p)
{
    if (!m_project) return;
    using namespace Darwin;

    const auto& flags = m_project->flags();
    if (flags.isEmpty() && m_dragging != DragFlag) return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    for (qint64 flagTick : flags) {
        // ドラッグ中のフラッグは元の位置には描画しない（移動先で描画）
        if (m_dragging == DragFlag && flagTick == m_dragFlagOrigTick) continue;

        int fx = static_cast<int>(flagTick * PIXELS_PER_TICK * m_zoomLevel);
        const int chordLaneTop = 22;

        // アニメーション中か確認
        float animT = -1.0f;
        for (const auto& anim : m_flagPlantAnims) {
            if (anim.flagTick == flagTick) {
                animT = anim.progress;
                break;
            }
        }

        if (animT >= 0.0f) {
            // easeOutBack: 行き過ぎて戻る動きで「刺さる」感
            float t = animT;
            float s = 1.70158f;
            float eased = 1.0f + (t - 1.0f) * (t - 1.0f) * ((s + 1.0f) * (t - 1.0f) + s);
            // 上から下へスライド: Yオフセット -30 → 0
            float yOffset = -30.0f * (1.0f - eased);
            // 透明度: 徐々に表示
            float opacity = qBound(0.0f, animT * 3.0f, 1.0f);

            p.save();
            p.setOpacity(opacity);
            p.translate(0, yOffset);

            // フラッグポール
            p.setPen(QPen(QColor("#FF3366"), 1.5));
            p.drawLine(fx, 0, fx, chordLaneTop);

            // フラッグ旗
            QPainterPath flagPath;
            flagPath.moveTo(fx, 0);
            flagPath.lineTo(fx + 10, 5);
            flagPath.lineTo(fx, 10);
            flagPath.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#FF3366"));
            p.drawPath(flagPath);

            p.restore();
        } else {
            // 通常描画
            p.setPen(QPen(QColor("#FF3366"), 1.5));
            p.drawLine(fx, 0, fx, chordLaneTop);

            QPainterPath flagPath;
            flagPath.moveTo(fx, 0);
            flagPath.lineTo(fx + 10, 5);
            flagPath.lineTo(fx, 10);
            flagPath.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#FF3366"));
            p.drawPath(flagPath);
        }
    }

    // ドラッグ中のフラッグを現在位置に描画
    if (m_dragging == DragFlag && m_dragFlagCurrTick >= 0) {
        int fx = static_cast<int>(m_dragFlagCurrTick * PIXELS_PER_TICK * m_zoomLevel);

        // ドラッグ中は半透明で描画（コード帯より上のみ）
        const int chordLaneTop = 22;
        p.setPen(QPen(QColor(255, 51, 102, 180), 1.5));
        p.drawLine(fx, 0, fx, chordLaneTop);

        QPainterPath flagPath;
        flagPath.moveTo(fx, 0);
        flagPath.lineTo(fx + 10, 5);
        flagPath.lineTo(fx, 10);
        flagPath.closeSubpath();

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 51, 102, 180));
        p.drawPath(flagPath);
    }

    p.restore();
}

void TimelineWidget::onLongPress()
{
    if (!m_project || !m_longPressActive) return;
    m_longPressActive = false;
    m_longPressFired = true;  // 長押し成立フラグを立てる

    using namespace Darwin;

    double posX = m_longPressPos.x();

    // 長押し位置に既存フラッグがあればドラッグ移動を開始
    const auto& flags = m_project->flags();
    for (qint64 flagTick : flags) {
        int fx = static_cast<int>(flagTick * PIXELS_PER_TICK * m_zoomLevel);
        if (qAbs(posX - fx) < 10) {
            m_dragging = DragFlag;
            m_dragFlagOrigTick = flagTick;
            m_dragFlagCurrTick = flagTick;
            setCursor(Qt::ClosedHandCursor);
            update();
            return;
        }
    }

    // 近くにフラッグがなければ新規追加
    qint64 flagTick = qMax(0LL, static_cast<qint64>(posX / (PIXELS_PER_TICK * m_zoomLevel)));
    m_project->addFlag(flagTick);
    startFlagPlantAnim(flagTick);
}

void TimelineWidget::startFlagPlantAnim(qint64 flagTick)
{
    FlagPlantAnim anim;
    anim.flagTick = flagTick;
    anim.startMs = m_flagAnimClock.elapsed();
    anim.progress = 0.0f;
    m_flagPlantAnims.append(anim);
    if (!m_flagAnimTimer.isActive()) {
        m_flagAnimTimer.start();
    }
}

void TimelineWidget::tickFlagAnimations()
{
    bool animating = false;

    // 軌跡のフェードアニメーション
    const float fadeStep = 0.12f; // フェード速度
    if (m_isPlaying) {
        if (m_trailOpacity < 1.0f) {
            m_trailOpacity = qMin(1.0f, m_trailOpacity + fadeStep);
            animating = true;
        }
    } else {
        if (m_trailOpacity > 0.0f) {
            m_trailOpacity = qMax(0.0f, m_trailOpacity - fadeStep);
            animating = true;
        }
    }

    // フラッグ配置アニメーション
    if (!m_flagPlantAnims.isEmpty()) {
        animating = true;
        qint64 now = m_flagAnimClock.elapsed();
        const float duration = 350.0f; // ms

        for (int i = m_flagPlantAnims.size() - 1; i >= 0; --i) {
            float elapsed = static_cast<float>(now - m_flagPlantAnims[i].startMs);
            m_flagPlantAnims[i].progress = qBound(0.0f, elapsed / duration, 1.0f);
            if (m_flagPlantAnims[i].progress >= 1.0f) {
                m_flagPlantAnims.removeAt(i);
            }
        }
    }

    if (animating) {
        update();
    } else {
        m_flagAnimTimer.stop();
    }
}

/**
 * @brief コード帯のキャッシュを再構築する
 *
 * ノート・クリップ・トラックの変更時にのみ呼ばれ、
 * paintEvent 毎の全ノート走査による O(N*M) 処理を回避する。
 */
void TimelineWidget::rebuildChordCache()
{
    m_cachedChordSpans.clear();
    m_chordCacheDirty = false;
    if (!m_project) return;

    using namespace Darwin;

    // 1) 全ノートを絶対位置で収集
    struct AbsNote {
        qint64 start;
        qint64 end;
        int pitchClass;
    };
    QList<AbsNote> allNotes;
    for (Track* track : m_project->tracks()) {
        if (!track->isVisible()) continue;
        for (Clip* clip : track->clips()) {
            for (Note* note : clip->notes()) {
                qint64 absStart = clip->startTick() + note->startTick();
                qint64 absEnd   = absStart + note->durationTicks();
                allNotes.append({absStart, absEnd, note->pitch() % 12});
            }
        }
    }

    if (allNotes.isEmpty()) return;

    // 2) 境界 tick を収集（重複排除・ソート済み）
    QList<qint64> boundaries;
    boundaries.reserve(allNotes.size() * 2);
    for (const auto& n : allNotes) {
        boundaries.append(n.start);
        boundaries.append(n.end);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

    // 3) 各区間のコードを判定してスパンに連結
    for (int i = 0; i + 1 < boundaries.size(); ++i) {
        qint64 segStart = boundaries[i];
        qint64 segEnd   = boundaries[i + 1];
        if (segEnd <= segStart) continue;

        QSet<int> pitchClasses;
        for (const auto& n : allNotes) {
            if (n.start < segEnd && n.end > segStart) {
                pitchClasses.insert(n.pitchClass);
            }
        }

        QString chordName = ChordDetector::detect(pitchClasses);
        if (chordName.isEmpty()) continue;

        if (!m_cachedChordSpans.isEmpty() && m_cachedChordSpans.last().name == chordName
            && m_cachedChordSpans.last().endTick == segStart) {
            m_cachedChordSpans.last().endTick = segEnd;
        } else {
            m_cachedChordSpans.append({segStart, segEnd, chordName});
        }
    }
}
