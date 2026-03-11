/**
 * @file ArrangementView.cpp
 * @brief ArrangementViewのコンストラクタ、プロジェクト管理、イベントフィルタ、ヘッダー再構築
 *
 * ヘッダー生成 → ArrangementView_Headers.cpp
 * D&Dリオーダー → ArrangementView_DragDrop.cpp
 */
#include "ArrangementView.h"
#include "ArrangementGridWidget.h"
#include "TimelineWidget.h"
#include "widgets/IconButton.h"
#include "widgets/CustomTooltip.h"
#include "models/Project.h"
#include "models/Track.h"
#include "common/Constants.h"
#include "common/FadeHelper.h"
#include "common/ThemeManager.h"
#include <QPainter>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <QScrollBar>
#include <QLineEdit>
#include <QPainterPath>
#include <QColorDialog>
#include <QMouseEvent>
#include <QEvent>
#include <QCoreApplication>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QTimer>

// selectTrack()でスタイル更新に使用するstatic関数
static void applyTrackHeaderStyle(QWidget* header, Track* t, Track* selectedTrack, Project* project)
{
    if (!header || !t || !project) return;
    using namespace Darwin;
    const ThemeManager& tm = ThemeManager::instance();
    const bool isDark = tm.isDarkMode();
    bool isSelected = (selectedTrack == t);
    bool isFolder = t->isFolder();
    bool isChild = (t->parentFolderId() >= 0);
    QString bgCol = isSelected
        ? (isDark ? "#2d3748" : "#e2e8f0")
        : tm.panelBackgroundColor().name();
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
    if (isFolder && !isChild) {
        bgCol = isSelected
            ? (isDark ? "#2d3748" : "#dde3ef")
            : (isDark ? "#2a2a3a" : "#eef1f6");
    }
    header->setStyleSheet(QString(
        "border-bottom: 1px solid %2; border-right: 1px solid %2; background-color: %1;"
    ).arg(bgCol, border));

    // QLineEdit の色も更新（フォルダ=12px、通常トラック=11px）
    const int leFontSize = isFolder ? 12 : 11;
    const QString leStyle = QString(
        "QLineEdit { font-family: 'Segoe UI', sans-serif; font-size: %5px; font-weight: 700;"
        " color: %1; background: transparent; border: 1px solid transparent;"
        " border-radius: 2px; padding: 2px; }"
        "QLineEdit:hover { border: 1px solid %2; }"
        "QLineEdit:focus { background: %3; border: 1px solid %4; }")
        .arg(tm.textColor().name())
        .arg(tm.borderColor().name())
        .arg(tm.panelBackgroundColor().name())
        .arg(isDark ? "#94a3b8" : "#3b82f6")
        .arg(leFontSize);
    for (QLineEdit* le : header->findChildren<QLineEdit*>()) {
        le->setStyleSheet(leStyle);
    }
}

// ─── コンストラクタ ──────────────────────────────────────

ArrangementView::ArrangementView(QWidget *parent) 
    : QWidget(parent)
    , m_grid(nullptr)
    , m_gridScroll(nullptr)
    , m_project(nullptr)
    , m_headersLayout(nullptr)
    , m_scrollContent(nullptr)
    , m_selectedTrack(nullptr)
    , m_timeline(nullptr)
{
    // 長押しタイマー設定（400ms 長押しでドラッグ開始）
    m_longPressTimer.setSingleShot(true);
    m_longPressTimer.setInterval(400);
    connect(&m_longPressTimer, &QTimer::timeout, this, [this]() {
        if (m_longPressTrack) {
            beginTrackDrag(m_longPressTrack);
        }
    });

    // メインレイアウト（縦方向）
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0,0,0,0);
    mainLayout->setSpacing(0);

    // 1. 上部タイムラインヘッダー（固定高さ）
    QWidget *topContainer = m_topContainer = new QWidget(this);
    topContainer->setFixedHeight(Darwin::ARRANGEMENT_TIMELINE_HEIGHT);
    topContainer->setStyleSheet(QString("background-color: %1; border-bottom: 1px solid %2;").arg(
        Darwin::ThemeManager::instance().panelBackgroundColor().name(),
        Darwin::ThemeManager::instance().borderColor().name()));
    
    QHBoxLayout *topLayout = new QHBoxLayout(topContainer);
    topLayout->setContentsMargins(0,0,0,0);
    topLayout->setSpacing(0);
    
    // ヘッダー幅分のスペーサー
    QWidget *cornerSpacer = m_cornerSpacer = new QWidget(topContainer);
    cornerSpacer->setFixedWidth(Darwin::ARRANGEMENT_HEADER_WIDTH);
    cornerSpacer->setStyleSheet(QString("background-color: %1; border-right: 1px solid %2;").arg(
        Darwin::ThemeManager::instance().panelBackgroundColor().name(),
        Darwin::ThemeManager::instance().borderColor().name()));
    
    QHBoxLayout *cornerLayout = new QHBoxLayout(cornerSpacer);
    cornerLayout->setContentsMargins(8, 2, 8, 2);
    
    auto makeCornerBtnStyle = [](const Darwin::ThemeManager& t) {
        bool d = t.isDarkMode();
        return QString(
            "QPushButton { background-color: %1; color: %2; border: 1px solid %3;"
            " border-radius: 4px; font-weight: 600; font-size: 11px; padding: 4px 8px; }"
            "QPushButton:hover { background-color: %4; color: %5; }"
            "QPushButton:pressed { background-color: %3; }")
            .arg(d ? "#2d3748" : "#f1f5f9",
                 d ? "#94a3b8" : "#64748b",
                 d ? "#334155" : "#cbd5e1",
                 d ? "#3d4f69" : "#e2e8f0",
                 d ? "#e2e8f0" : "#1e293b");
    };
    const QString cornerBtnStyle = makeCornerBtnStyle(Darwin::ThemeManager::instance());

    QPushButton *addTrackBtn = m_addTrackBtn = new QPushButton("+ Track");
    addTrackBtn->setStyleSheet(cornerBtnStyle);
    connect(addTrackBtn, &QPushButton::clicked, this, [this]() {
        if (m_project) m_project->addTrack("New Track");
    });
    cornerLayout->addWidget(addTrackBtn, 0, Qt::AlignVCenter);

    QPushButton *addFolderBtn = m_addFolderBtn = new QPushButton("+ Folder");
    addFolderBtn->setStyleSheet(cornerBtnStyle);;
    connect(addFolderBtn, &QPushButton::clicked, this, [this]() {
        if (m_project) m_project->addFolderTrack("New Folder");
    });
    cornerLayout->addWidget(addFolderBtn, 0, Qt::AlignVCenter);

    cornerLayout->addStretch();

    // グリッドスナップ切り替えボタン (磁石アイコン)
    m_snapBtn = new IconButton(IconButton::Magnet, cornerSpacer);
    m_snapBtn->setToolTip("Grid Snap (G)");
    connect(m_snapBtn, &QPushButton::toggled, this, [this](bool checked) {
        if (m_project) m_project->setGridSnapEnabled(checked);
    });
    cornerLayout->addWidget(m_snapBtn, 0, Qt::AlignVCenter);
    
    topLayout->addWidget(cornerSpacer);
    
    // 小節番号タイムライン（スクロールエリア内）
    QScrollArea *timeScroll = new QScrollArea(topContainer);
    timeScroll->setFrameShape(QFrame::NoFrame);
    timeScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    timeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    timeScroll->setWidgetResizable(true);

    TimelineWidget *timeline = new TimelineWidget(timeScroll);
    m_timeline = timeline;
    timeScroll->setWidget(timeline);
    topLayout->addWidget(timeScroll);
    
    mainLayout->addWidget(topContainer);

    // 2. 分割エリア（ヘッダー | グリッド）
    QWidget *splitArea = new QWidget(this);
    QHBoxLayout *splitLayout = new QHBoxLayout(splitArea);
    splitLayout->setContentsMargins(0,0,0,0);
    splitLayout->setSpacing(0);

    // 左: ヘッダースクロールエリア
    m_headerScroll = new QScrollArea(splitArea);
    m_headerScroll->setFixedWidth(Darwin::ARRANGEMENT_HEADER_WIDTH);
    m_headerScroll->setFrameShape(QFrame::NoFrame);
    m_headerScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_headerScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_headerScroll->setWidgetResizable(true);
    m_headerScroll->viewport()->installEventFilter(this); // ホイールイベント転送用

    QWidget *headerContent = new QWidget(m_headerScroll);
    m_headerContent = headerContent;
    headerContent->setStyleSheet(QString("background-color: %1;").arg(
        Darwin::ThemeManager::instance().panelBackgroundColor().name()));
    m_headersLayout = new QVBoxLayout(headerContent);
    m_headersLayout->setContentsMargins(0,0,0,0);
    m_headersLayout->setSpacing(0);
    m_headersLayout->addStretch();

    m_headerScroll->setWidget(headerContent);
    splitLayout->addWidget(m_headerScroll);

    // 右: グリッドスクロールエリア
    m_gridScroll = new QScrollArea(splitArea);
    m_gridScroll->setFrameShape(QFrame::NoFrame);
    m_gridScroll->setWidgetResizable(true);
    
    m_grid = new ArrangementGridWidget(m_gridScroll);
    m_grid->setScrollArea(m_gridScroll);
    m_gridScroll->setWidget(m_grid);
    splitLayout->addWidget(m_gridScroll);

    mainLayout->addWidget(splitArea);

    // 垂直スクロールバー同期
    connect(m_gridScroll->verticalScrollBar(), &QScrollBar::valueChanged,
            m_headerScroll->verticalScrollBar(), &QScrollBar::setValue);
    connect(m_headerScroll->verticalScrollBar(), &QScrollBar::valueChanged,
            m_gridScroll->verticalScrollBar(), &QScrollBar::setValue);

    // 水平スクロールバー同期（タイムライン）
    connect(m_gridScroll->horizontalScrollBar(), &QScrollBar::valueChanged,
            timeScroll->horizontalScrollBar(), &QScrollBar::setValue);
    connect(timeScroll->horizontalScrollBar(), &QScrollBar::valueChanged,
            m_gridScroll->horizontalScrollBar(), &QScrollBar::setValue);

    // テーマ変更時にヘッダー全体を再構築
    connect(&Darwin::ThemeManager::instance(), &Darwin::ThemeManager::themeChanged,
            this, [this]() {
        const Darwin::ThemeManager& tm2 = Darwin::ThemeManager::instance();
        const QString panelBg  = tm2.panelBackgroundColor().name();
        const QString borderCol = tm2.borderColor().name();

        if (m_topContainer)
            m_topContainer->setStyleSheet(
                QString("background-color: %1; border-bottom: 1px solid %2;").arg(panelBg, borderCol));
        if (m_cornerSpacer)
            m_cornerSpacer->setStyleSheet(
                QString("background-color: %1; border-right: 1px solid %2;").arg(panelBg, borderCol));
        if (m_headerContent)
            m_headerContent->setStyleSheet(
                QString("background-color: %1;").arg(panelBg));

        // +Track / +Folder ボタン
        const bool d = tm2.isDarkMode();
        const QString btnStyle = QString(
            "QPushButton { background-color: %1; color: %2; border: 1px solid %3;"
            " border-radius: 4px; font-weight: 600; font-size: 11px; padding: 4px 8px; }"
            "QPushButton:hover { background-color: %4; color: %5; }"
            "QPushButton:pressed { background-color: %3; }")
            .arg(d ? "#2d3748" : "#f1f5f9",
                 d ? "#94a3b8" : "#64748b",
                 d ? "#334155" : "#cbd5e1",
                 d ? "#3d4f69" : "#e2e8f0",
                 d ? "#e2e8f0" : "#1e293b");
        if (m_addTrackBtn)  m_addTrackBtn->setStyleSheet(btnStyle);
        if (m_addFolderBtn) m_addFolderBtn->setStyleSheet(btnStyle);

        // 全トラックヘッダーのスタイルを更新
        for (auto it = m_trackHeaders.begin(); it != m_trackHeaders.end(); ++it) {
            applyTrackHeaderStyle(it.value(), it.key(), m_selectedTrack, m_project);
            // アイコンボタンのhoverスタイルをテーマに合わせて更新
            const QString hoverGeneral = d ? "#2d3748" : "#e2e8f0";
            const QString hoverDanger  = d ? "#3d1515" : "#fee2e2";
            const QString btnBase = "QPushButton { border: none; background: transparent; } QPushButton:hover { background: %1; border-radius: 4px; }";
            if (auto* btn = it.value()->findChild<QAbstractButton*>("folderExpandBtn"))
                btn->setStyleSheet(QString(btnBase).arg(hoverGeneral));
            if (auto* btn = it.value()->findChild<QAbstractButton*>("eyeBtn"))
                btn->setStyleSheet(QString(btnBase).arg(hoverDanger));
            if (auto* btn = it.value()->findChild<QAbstractButton*>("delBtn"))
                btn->setStyleSheet(QString(btnBase).arg(hoverDanger));
        }
    });

    // グリッドの幅変更時にタイムラインも同期
    connect(m_grid, &ArrangementGridWidget::widthChanged, timeline, &TimelineWidget::syncWidthToGrid);

    // グリッドのズーム変更時にタイムラインも同期
    connect(m_grid, &ArrangementGridWidget::zoomChanged, timeline, &TimelineWidget::setZoomLevel);
}

QScrollBar* ArrangementView::horizontalScrollBar() const
{
    return m_gridScroll ? m_gridScroll->horizontalScrollBar() : nullptr;
}

// ─── プロジェクト管理 ────────────────────────────────────

void ArrangementView::setProject(Project* project)
{
    m_project = project;
    if (m_grid) {
        m_grid->setProject(project);
    }
    
    // タイムラインにプロジェクトを設定
    if (m_timeline) {
        auto* tw = static_cast<TimelineWidget*>(m_timeline);
        tw->setProject(project);
        tw->setGridScrollArea(m_gridScroll);
    }

    connect(m_project, &Project::trackAdded, this, &ArrangementView::onTrackAdded);
    connect(m_project, &Project::trackRemoved, this, &ArrangementView::onTrackRemoved);
    connect(m_project, &Project::trackOrderChanged, this, &ArrangementView::rebuildHeaders);
    connect(m_project, &Project::folderStructureChanged, this, &ArrangementView::rebuildHeaders);
    
    // スナップボタンをプロジェクトに同期
    if (m_snapBtn) {
        m_snapBtn->setChecked(m_project->gridSnapEnabled());
        connect(m_project, &Project::gridSnapChanged, m_snapBtn, [this](bool enabled) {
            m_snapBtn->blockSignals(true);
            m_snapBtn->setChecked(enabled);
            m_snapBtn->blockSignals(false);
        });
    }
    
    // ヘッダーをクリアして再構築
    QLayoutItem *child;
    while ((child = m_headersLayout->takeAt(0)) != 0) {
        if (child->widget()) delete child->widget();
        delete child;
    }
    m_trackHeaders.clear();
    m_headersLayout->addStretch();
    
    // 既存トラックを追加（アニメーションなし）
    m_initializing = true;
    for (int i = 0; i < m_project->trackCount(); ++i) {
        onTrackAdded(m_project->trackAt(i));
    }
    m_initializing = false;

    // デフォルト選択
    if (m_project->trackCount() > 0) {
        selectTrack(m_project->trackAt(0));
    }

    // 初期幅を同期
    if (m_grid && m_timeline) {
        auto* tw = static_cast<TimelineWidget*>(m_timeline);
        tw->syncWidthToGrid(m_grid->minimumWidth());
    }
}

void ArrangementView::selectTrack(Track* track)
{
    if (m_selectedTrack == track) return;

    Track* oldTrack = m_selectedTrack;
    m_selectedTrack = track;
    
    if (oldTrack && m_trackHeaders.contains(oldTrack)) {
        applyTrackHeaderStyle(m_trackHeaders[oldTrack], oldTrack, m_selectedTrack, m_project);
    }
    if (m_selectedTrack && m_trackHeaders.contains(m_selectedTrack)) {
        applyTrackHeaderStyle(m_trackHeaders[m_selectedTrack], m_selectedTrack, m_selectedTrack, m_project);
    }

    emit trackSelected(m_selectedTrack);
}

// ─── トラック追加/削除 ───────────────────────────────────

void ArrangementView::onTrackAdded(Track* track)
{
    QWidget* trackWidget = track->isFolder() ? createFolderHeader(track) : createTrackHeader(track);
    
    m_trackHeaders[track] = trackWidget;
    
    // ストレッチの前に挿入
    int count = m_headersLayout->count();
    m_headersLayout->insertWidget(count - 1, trackWidget);

    // フォルダの子トラックで、親が折りたたまれている場合は非表示
    bool hiddenByFolder = !m_project->isTrackVisibleInHierarchy(track);
    if (hiddenByFolder) {
        trackWidget->setVisible(false);
    }

    // トラック追加アニメーション
    if (!m_initializing && !hiddenByFolder) {
        FadeHelper::fadeIn(trackWidget, 400, QEasingCurve::OutCubic);
    }
}

void ArrangementView::onTrackRemoved(Track* track)
{
    if (m_trackHeaders.contains(track)) {
        QWidget* widget = m_trackHeaders.take(track);

        // フェードアウトアニメーション
        FadeHelper::fadeOut(widget, 350, QEasingCurve::InOutQuad, [this, widget]() {
            m_headersLayout->removeWidget(widget);
            widget->hide();
            widget->deleteLater();
        });
    }
    
    if (m_selectedTrack == track) {
        m_selectedTrack = nullptr;
        if (!m_trackHeaders.isEmpty()) {
            selectTrack(m_trackHeaders.keys().first());
        } else {
            emit trackSelected(nullptr);
        }
    }
}

// ─── イベントフィルタ ────────────────────────────────────

bool ArrangementView::eventFilter(QObject* watched, QEvent* event)
{
    // トラックD&D中はマウスイベントを横取り
    if (m_isDraggingTrack) {
        if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            updateTrackDrag(me->globalPosition().toPoint());
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            finishTrackDrag();
            return true;
        }
        return true; // ドラッグ中は他のイベントをブロック
    }

    // ヘッダー領域のホイールイベントをグリッドスクロールに転送
    if (event->type() == QEvent::Wheel && m_headerScroll && watched == m_headerScroll->viewport()) {
        auto* we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            // Ctrl+ホイールはズーム処理のためグリッドに転送
            QCoreApplication::sendEvent(m_grid, we);
            return true;
        }
        QScrollBar* sb = m_gridScroll->verticalScrollBar();
        sb->setValue(sb->value() - we->angleDelta().y());
        return true;
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);

        // QLineEditなどの入力ウィジェットでは長押しを開始しない
        if (qobject_cast<QLineEdit*>(watched)) {
            for (auto it = m_trackHeaders.begin(); it != m_trackHeaders.end(); ++it) {
                QWidget* header = it.value();
                if (watched == header || header->isAncestorOf(static_cast<QWidget*>(watched))) {
                    selectTrack(it.key());
                    break;
                }
            }
            return QWidget::eventFilter(watched, event);
        }
        for (auto it = m_trackHeaders.begin(); it != m_trackHeaders.end(); ++it) {
            QWidget* header = it.value();
            if (watched == header || header->isAncestorOf(static_cast<QWidget*>(watched))) {
                selectTrack(it.key());
                startLongPressTimer(it.key(), me->globalPosition().toPoint());
                break;
            }
        }
    }

    if (event->type() == QEvent::MouseMove) {
        // 長押し中に大きく動いたらキャンセル
        if (m_longPressTimer.isActive()) {
            auto* me = static_cast<QMouseEvent*>(event);
            QPoint diff = me->globalPosition().toPoint() - m_longPressStartPos;
            if (diff.manhattanLength() > 10) {
                cancelLongPress();
            }
        }
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        cancelLongPress();
    }

    return QWidget::eventFilter(watched, event);
}

// ─── ヘッダー再構築 ─────────────────────────────────────

void ArrangementView::rebuildHeaders()
{
    if (!m_project) return;
    
    // レイアウトからすべてのウィジェットを除去
    while (m_headersLayout->count() > 0) {
        QLayoutItem* item = m_headersLayout->takeAt(0);
        if (!item->widget()) {
            delete item;
            continue;
        }
        item->widget()->setParent(nullptr);
        delete item;
    }
    
    // プロジェクトの順序で再挿入（フォルダ展開状態に応じて表示制御）
    for (int i = 0; i < m_project->trackCount(); ++i) {
        Track* track = m_project->trackAt(i);
        
        // ヘッダーが存在しない場合は生成
        if (!m_trackHeaders.contains(track)) {
            QWidget* header = track->isFolder() ? createFolderHeader(track) : createTrackHeader(track);
            m_trackHeaders[track] = header;
        }
        
        QWidget* header = m_trackHeaders[track];
        header->setParent(m_headersLayout->parentWidget());
        
        // フォルダの子で親が折りたたまれている場合は非表示
        bool visible = m_project->isTrackVisibleInHierarchy(track);
        header->setVisible(visible);
        
        m_headersLayout->addWidget(header);
    }
    m_headersLayout->addStretch();
    
    // グリッドの高さを再計算
    if (m_grid) m_grid->updateDynamicSize();
}

void ArrangementView::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
}
