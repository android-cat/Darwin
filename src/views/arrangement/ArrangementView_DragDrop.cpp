/**
 * @file ArrangementView_DragDrop.cpp
 * @brief トラックD&Dリオーダーのロジック（長押し→ドラッグ→ドロップ）
 */
#include "ArrangementView.h"
#include "ArrangementGridWidget.h"
#include "models/Project.h"
#include "models/Track.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include "common/Constants.h"
#include "common/ThemeManager.h"

// _Headers.cppと同じstatic関数（D&D中のスタイルリセットに使用）
static void applyTrackHeaderStyle(QWidget* header, Track* t, Track* selectedTrack, Project* project)
{
    if (!header || !t || !project) return;
    using namespace Darwin;
    const ThemeManager& tm = ThemeManager::instance();
    bool isSelected = (selectedTrack == t);
    bool isFolder = t->isFolder();
    bool isChild = (t->parentFolderId() >= 0);
    bool isDark = tm.isDarkMode();
    QString bgCol = isSelected ? (isDark ? "#2d3748" : "#e2e8f0") : tm.panelBackgroundColor().name();
    if (isChild) {
        Track* parentFolder = project->trackById(t->parentFolderId());
        if (parentFolder) {
            QColor fColor = parentFolder->color();
            int alpha = isSelected ? 40 : 15;
            if (isFolder) alpha = isSelected ? 60 : 30;
            bgCol = QString("rgba(%1, %2, %3, %4)").arg(fColor.red()).arg(fColor.green()).arg(fColor.blue()).arg(alpha/255.0f);
        }
    }
    const QString border = tm.borderColor().name();
    if (isFolder) {
        if (!isChild) bgCol = isSelected ? (isDark ? "#2d3748" : "#dde3ef") : (isDark ? "#2a2a3a" : "#eef1f6");
        header->setStyleSheet(QString("border-bottom: 1px solid %2; border-right: 1px solid %2; background-color: %1;").arg(bgCol, border));
    } else {
        header->setStyleSheet(QString("border-bottom: 1px solid %2; border-right: 1px solid %2; background-color: %1;").arg(bgCol, border));
    }
}

// ─── 長押しタイマー ──────────────────────────────────────

void ArrangementView::startLongPressTimer(Track* track, const QPoint& globalPos)
{
    m_longPressTrack = track;
    m_longPressStartPos = globalPos;
    m_longPressTimer.start();
}

void ArrangementView::cancelLongPress()
{
    m_longPressTimer.stop();
    m_longPressTrack = nullptr;
}

// ─── トラックドラッグ開始: つかみ上げアニメーション ──────

void ArrangementView::beginTrackDrag(Track* track)
{
    if (!m_project || !m_trackHeaders.contains(track)) return;
    
    m_isDraggingTrack = true;
    m_dragTrack = track;
    
    QWidget* header = m_trackHeaders[track];
    
    // つかみ上げアニメーション: 元のヘッダーを半透明に
    header->setStyleSheet(QString("border-bottom: 1px solid %1; border-right: 1px solid %1; background-color: %2; opacity: 0.4;")
        .arg(Darwin::ThemeManager::instance().borderColor().name(),
             Darwin::ThemeManager::instance().gridLineColor().name()));
    auto* dimEffect = new QGraphicsOpacityEffect(header);
    dimEffect->setOpacity(0.3);
    header->setGraphicsEffect(dimEffect);
    
    // 浮遊ゴーストを作成 (トップレベルウィジェット)
    m_dragGhost = new QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
    m_dragGhost->setAttribute(Qt::WA_TranslucentBackground);
    m_dragGhost->setFixedSize(header->width(), header->height());
    
    // ゴーストの中身を描画
    QPixmap pixmap(header->size() * header->devicePixelRatioF());
    pixmap.setDevicePixelRatio(header->devicePixelRatioF());
    pixmap.fill(Qt::transparent);
    header->render(&pixmap);
    
    QLabel* ghostLabel = new QLabel(m_dragGhost);
    ghostLabel->setPixmap(pixmap);
    ghostLabel->setFixedSize(header->size());
    ghostLabel->move(0, 0);
    
    {
        bool isDark = Darwin::ThemeManager::instance().isDarkMode();
        m_dragGhost->setStyleSheet(QString(
            "background-color: %1; border: 2px solid #3b82f6; border-radius: 6px;"
        ).arg(isDark ? "rgba(37,37,38,220)" : "rgba(241,245,249,230)"));
    }
    
    // ゴースト位置をカーソル付近に
    QPoint headerGlobal = header->mapToGlobal(QPoint(0, 0));
    m_dragOffset = m_longPressStartPos - headerGlobal;
    m_dragGhost->move(m_longPressStartPos - m_dragOffset);
    m_dragGhost->show();
    
    // ゴーストが浮き上がるアニメーション
    m_dragGhost->setWindowOpacity(0.0);
    auto* floatAnim = new QPropertyAnimation(m_dragGhost, "windowOpacity", m_dragGhost);
    floatAnim->setDuration(200);
    floatAnim->setStartValue(0.0);
    floatAnim->setEndValue(0.92);
    floatAnim->setEasingCurve(QEasingCurve::OutCubic);
    floatAnim->start(QAbstractAnimation::DeleteWhenStopped);
    
    // 挿入インジケーター
    m_dropIndicator = new QWidget(m_headersLayout->parentWidget());
    m_dropIndicator->setFixedHeight(3);
    m_dropIndicator->setStyleSheet("background-color: #3b82f6; border-radius: 1px;");
    m_dropIndicator->hide();
    
    m_dragInsertIndex = m_project->trackIndex(track);
    
    // マウスをキャプチャ
    if (m_trackHeaders.contains(track)) {
        m_trackHeaders[track]->grabMouse();
    }
}

// ─── ドラッグ中の更新 ───────────────────────────────────

void ArrangementView::updateTrackDrag(const QPoint& globalPos)
{
    if (!m_isDraggingTrack || !m_dragGhost || !m_project) return;
    
    // ゴーストをカーソルに追従
    m_dragGhost->move(globalPos - m_dragOffset);
    
    // ヘッダーレイアウト上でのY座標から挿入先を計算
    QWidget* layoutParent = m_headersLayout->parentWidget();
    if (!layoutParent) return;
    
    QPoint localPos = layoutParent->mapFromGlobal(globalPos);
    const int rowHeight = Darwin::ARRANGEMENT_TRACK_ROW_HEIGHT;
    
    // 前回のフォルダハイライトをリセット
    if (m_dragTargetFolder && m_trackHeaders.contains(m_dragTargetFolder)) {
        applyTrackHeaderStyle(m_trackHeaders[m_dragTargetFolder], m_dragTargetFolder, m_selectedTrack, m_project);
    }
    m_dragTargetFolder = nullptr;
    
    // 表示中トラックのリストを構築
    struct VisibleEntry { Track* track; int flatIndex; };
    QList<VisibleEntry> visibleTracks;
    for (int i = 0; i < m_project->trackCount(); ++i) {
        Track* t = m_project->trackAt(i);
        if (m_project->isTrackVisibleInHierarchy(t)) {
            visibleTracks.append({t, i});
        }
    }
    
    // カーソル位置から表示行インデックスとY位置を計算
    int visibleRow = qBound(0, localPos.y() / rowHeight, visibleTracks.size() - 1);
    int yInRow = localPos.y() - visibleRow * rowHeight;
    
    // フォルダ上の中央エリアにホバー中 → フォルダドロップ
    if (visibleRow >= 0 && visibleRow < visibleTracks.size()) {
        Track* hoverTrack = visibleTracks[visibleRow].track;
        bool isCycle = m_project->isDescendant(hoverTrack, m_dragTrack);
        if (hoverTrack->isFolder() && m_dragTrack 
            && hoverTrack != m_dragTrack && !isCycle
            && yInRow > rowHeight * 0.2 && yInRow < rowHeight * 0.8) {
            // フォルダハイライト
            m_dragTargetFolder = hoverTrack;
            if (m_trackHeaders.contains(hoverTrack)) {
                bool isDark = Darwin::ThemeManager::instance().isDarkMode();
                m_trackHeaders[hoverTrack]->setStyleSheet(QString(
                    "border: 2px solid #8b5cf6; border-radius: 4px;"
                    "background-color: %1;"
                ).arg(isDark ? "#2d1d4a" : "#ede9fe"));
            }
            if (m_dropIndicator) m_dropIndicator->hide();
            m_dragInsertIndex = -1;
            return;
        }
    }
    
    // 通常の挿入位置計算
    int insertVisibleRow = 0;
    for (int i = 0; i < visibleTracks.size(); ++i) {
        int rowTop = i * rowHeight;
        if (localPos.y() < rowTop + rowHeight / 2) break;
        insertVisibleRow = i + 1;
    }
    
    // 表示行インデックスをフラットインデックスに変換
    if (insertVisibleRow >= visibleTracks.size()) {
        m_dragInsertIndex = m_project->trackCount();
    } else {
        m_dragInsertIndex = visibleTracks[insertVisibleRow].flatIndex;
    }
    
    // インジケーター表示
    if (m_dropIndicator) {
        int indicatorY = insertVisibleRow * rowHeight - 1;
        m_dropIndicator->setGeometry(0, indicatorY, layoutParent->width(), 3);
        m_dropIndicator->show();
        m_dropIndicator->raise();
    }
}

// ─── ドラッグ終了 ────────────────────────────────────────

void ArrangementView::finishTrackDrag()
{
    if (!m_isDraggingTrack) return;
    
    // マウスリリース
    if (m_dragTrack && m_trackHeaders.contains(m_dragTrack)) {
        m_trackHeaders[m_dragTrack]->releaseMouse();
    }
    
    // フォルダハイライトをリセット
    if (m_dragTargetFolder && m_trackHeaders.contains(m_dragTargetFolder)) {
        applyTrackHeaderStyle(m_trackHeaders[m_dragTargetFolder], m_dragTargetFolder, m_selectedTrack, m_project);
    }
    
    // ゴーストのフェードアウト
    if (m_dragGhost) {
        auto* fadeOut = new QPropertyAnimation(m_dragGhost, "windowOpacity", m_dragGhost);
        fadeOut->setDuration(150);
        fadeOut->setStartValue(m_dragGhost->windowOpacity());
        fadeOut->setEndValue(0.0);
        fadeOut->setEasingCurve(QEasingCurve::InCubic);
        connect(fadeOut, &QPropertyAnimation::finished, m_dragGhost, &QWidget::deleteLater);
        fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
        m_dragGhost = nullptr;
    }
    
    // インジケーター削除
    if (m_dropIndicator) {
        m_dropIndicator->deleteLater();
        m_dropIndicator = nullptr;
    }
    
    // 元のヘッダーを復元
    if (m_dragTrack && m_trackHeaders.contains(m_dragTrack)) {
        QWidget* header = m_trackHeaders[m_dragTrack];
        header->setGraphicsEffect(nullptr);
    }
    
    // ── ドロップ処理 ──
    if (m_project && m_dragTrack) {
        if (m_dragTargetFolder) {
            // フォルダにドロップ
            m_project->addTrackToFolder(m_dragTrack, m_dragTargetFolder);
        } else if (m_dragInsertIndex >= 0) {
            if (m_dragTrack->isFolder()) {
                // フォルダブロック一括移動
                m_project->moveFolderBlock(m_dragTrack, m_dragInsertIndex);
            } else {
                // 通常トラック移動
                int fromIndex = m_project->trackIndex(m_dragTrack);
                int toIndex = m_dragInsertIndex;
                if (toIndex > fromIndex) toIndex--;
                if (fromIndex != toIndex && fromIndex >= 0) {
                    if (m_dragTrack->parentFolderId() >= 0) {
                        m_project->removeTrackFromFolder(m_dragTrack);
                    }
                    m_project->moveTrack(m_project->trackIndex(m_dragTrack), toIndex);
                }
            }
        }
    }
    
    m_isDraggingTrack = false;
    m_dragTrack = nullptr;
    m_dragInsertIndex = -1;
    m_dragTargetFolder = nullptr;
    
    // ヘッダースタイルを完全に再構築
    rebuildHeaders();
}
