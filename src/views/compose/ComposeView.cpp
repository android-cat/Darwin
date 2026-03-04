#include "ComposeView.h"
#include "ArrangementView.h"
#include "PianoRollView.h"
#include "ArrangementGridWidget.h"
#include "PianoRollGridWidget.h"
#include "PluginEditorWidget.h"
#include "models/Track.h"
#include "Project.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QListWidget>
#include <QListWidgetItem>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include "common/ThemeManager.h"

ComposeView::ComposeView(QWidget *parent)
    : QWidget(parent)
    , m_arrangementView(nullptr)
    , m_arrangementGrid(nullptr)
    , m_pianoRollGrid(nullptr)
    , m_pianoRollView(nullptr)
    , m_editorStack(nullptr)
    , m_pluginEditor(nullptr)
    , m_noPluginPanel(nullptr)
    , m_noPluginLabel(nullptr)
    , m_pluginSelectorPanel(nullptr)
    , m_selectorTrackLabel(nullptr)
    , m_pluginListWidget(nullptr)
    , m_selectorTitle(nullptr)
    , m_loadPluginBtn(nullptr)
    , m_rescanBtn(nullptr)
    , m_scanStatusLabel(nullptr)
    , m_editorContainer(nullptr)
    , m_editorTrackLabel(nullptr)
    , m_detachBtn(nullptr)
    , m_scanner(new VST3Scanner(this))
    , m_selectedPluginIndex(-1)
    , m_currentTrack(nullptr)
{
    setupUi();
    connect(&Darwin::ThemeManager::instance(), &Darwin::ThemeManager::themeChanged,
            this, &ComposeView::applyTheme);
    // 初回テーマ適用（起動時のモードに合わせてスタイルを確定させる）
    applyTheme();
}

void ComposeView::setProject(Project* project)
{
    if (m_arrangementView) {
        m_arrangementView->setProject(project);
    }
    if (m_pianoRollView) {
        m_pianoRollView->setProject(project);
    }
}

void ComposeView::setUndoStack(QUndoStack* stack)
{
    if (m_arrangementGrid) {
        m_arrangementGrid->setUndoStack(stack);
    }
    if (m_pianoRollGrid) {
        m_pianoRollGrid->setUndoStack(stack);
    }
}

void ComposeView::onTrackSelected(Track* track)
{
    // 別トラックへの切り替え時は既存エディタを閉じる
    if (m_currentTrack != track && m_pluginEditor && m_pluginEditor->isEditorOpen()) {
        m_pluginEditor->closeEditor();
    }
    m_currentTrack = track;

    if (!track) {
        m_noPluginLabel->setText("Select a track to start");
        m_editorStack->setCurrentWidget(m_noPluginPanel);
        return;
    }

    if (!track->hasPlugin()) {
        // プラグイン未設定 → プラグイン選択パネルを表示
        m_selectorTrackLabel->setText(track->name());
        m_selectedPluginIndex = -1;
        m_loadPluginBtn->setEnabled(false);
        refreshPluginList();
        m_editorStack->setCurrentWidget(m_pluginSelectorPanel);
        return;
    }

    // プラグイン設定済み → エディタコンテナを表示
    openEditorForCurrentTrack();
}

void ComposeView::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->setHandleWidth(1);
    m_mainSplitter->setStyleSheet("QSplitter::handle { background-color: #334155; }");

    m_vSplitter = new QSplitter(Qt::Vertical, m_mainSplitter);
    m_vSplitter->setHandleWidth(1);
    m_vSplitter->setStyleSheet("QSplitter::handle { background-color: #334155; }");

    // アレンジメントビュー（上段）
    m_arrangementView = new ArrangementView(m_vSplitter);
    m_arrangementGrid = m_arrangementView->gridWidget();

    // ピアノロール（下段）
    m_pianoRollView = new PianoRollView(m_vSplitter);
    m_pianoRollGrid = m_pianoRollView->gridWidget();

    m_vSplitter->addWidget(m_arrangementView);
    m_vSplitter->addWidget(m_pianoRollView);
    m_vSplitter->setStretchFactor(0, 2);
    m_vSplitter->setStretchFactor(1, 1);
    m_vSplitter->setSizes(QList<int>() << 600 << 200);

    // ========== 右パネル（3ページ構成） ==========
    m_editorStack = new QStackedWidget(m_mainSplitter);

    // ----- ページ0: トラック未選択 -----
    m_noPluginPanel = new QWidget(m_editorStack);
    m_noPluginPanel->setStyleSheet(QString("background-color: %1; border-left: 1px solid %2;").arg(
        Darwin::ThemeManager::instance().panelBackgroundColor().name(),
        Darwin::ThemeManager::instance().borderColor().name()));
    QVBoxLayout *noPluginLayout = new QVBoxLayout(m_noPluginPanel);
    m_noPluginLabel = new QLabel("Select a track to start", m_noPluginPanel);
    m_noPluginLabel->setAlignment(Qt::AlignCenter);
    m_noPluginLabel->setStyleSheet("color: #94a3b8; font-size: 14px;");
    noPluginLayout->addWidget(m_noPluginLabel);

    // ----- ページ1: プラグイン選択パネル -----
    m_pluginSelectorPanel = new QWidget(m_editorStack);
    m_pluginSelectorPanel->setStyleSheet(QString("background-color: %1; border-left: 1px solid %2;").arg(
        Darwin::ThemeManager::instance().panelBackgroundColor().name(),
        Darwin::ThemeManager::instance().borderColor().name()));
    QVBoxLayout *selectorLayout = new QVBoxLayout(m_pluginSelectorPanel);
    selectorLayout->setContentsMargins(16, 16, 16, 16);
    selectorLayout->setSpacing(12);

    // トラック名ヘッダー
    m_selectorTrackLabel = new QLabel("", m_pluginSelectorPanel);
    m_selectorTrackLabel->setStyleSheet("font-size: 13px; font-weight: 600; color: #e2e8f0;");

    // プラグインリストタイトル & スキャン行
    m_selectorTitle = new QLabel("VST3 INSTRUMENTS", m_pluginSelectorPanel);
    m_selectorTitle->setStyleSheet(
        "font-size: 9px; font-weight: bold; color: #94a3b8; letter-spacing: 1px;");

    QHBoxLayout *scanRow = new QHBoxLayout();
    m_scanStatusLabel = new QLabel("", m_pluginSelectorPanel);
    m_scanStatusLabel->setStyleSheet("font-size: 10px; color: #FF3366; font-weight: bold;");
    m_rescanBtn = new QPushButton("SCAN", m_pluginSelectorPanel);
    m_rescanBtn->setObjectName("composeScanBtn");
    m_rescanBtn->setFixedSize(56, 26);
    m_rescanBtn->setStyleSheet(R"(
        #composeScanBtn {
            border: none; background-color: transparent; color: #94a3b8;
            font-size: 10px; font-weight: bold; padding: 0px 4px;
        }
        #composeScanBtn:hover { color: #FF3366; }
        #composeScanBtn:disabled { color: #cbd5e1; }
    )");
    m_rescanBtn->setCursor(Qt::PointingHandCursor);
    connect(m_rescanBtn, &QPushButton::clicked, this, &ComposeView::onRescanClicked);
    scanRow->addWidget(m_scanStatusLabel);
    scanRow->addStretch();
    scanRow->addWidget(m_rescanBtn);

    // プラグインリスト
    m_pluginListWidget = new QListWidget(m_pluginSelectorPanel);
    m_pluginListWidget->setFocusPolicy(Qt::NoFocus);
    m_pluginListWidget->setStyleSheet(R"(
        QListWidget {
            border: 1px solid #334155; border-radius: 6px;
            background-color: #1e1e1e; font-size: 12px; outline: none;
        }
        QListWidget::item {
            padding: 10px 12px; border-bottom: 1px solid #252526; color: #e2e8f0;
        }
        QListWidget::item:selected { background-color: #2d1a23; color: #e2e8f0; }
        QListWidget::item:hover:!selected { background-color: #2d3748; }
    )");;
    connect(m_pluginListWidget, &QListWidget::itemClicked,
            this, &ComposeView::onPluginItemClicked);

    // ASSIGN ボタン
    m_loadPluginBtn = new QPushButton("ASSIGN PLUGIN", m_pluginSelectorPanel);
    m_loadPluginBtn->setObjectName("composeAssignBtn");
    m_loadPluginBtn->setFixedHeight(40);
    m_loadPluginBtn->setCursor(Qt::PointingHandCursor);
    m_loadPluginBtn->setEnabled(false);
    m_loadPluginBtn->setStyleSheet(R"(
        #composeAssignBtn {
            background-color: #334155; color: #e2e8f0; border: none;
            border-radius: 6px; font-size: 11px; font-weight: bold; letter-spacing: 1px;
        }
        #composeAssignBtn:hover { background-color: #FF3366; color: #ffffff; }
        #composeAssignBtn:disabled { background-color: #2a2a3a; color: #475569; }
    )");;
    connect(m_loadPluginBtn, &QPushButton::clicked, this, &ComposeView::onLoadPluginClicked);

    selectorLayout->addWidget(m_selectorTrackLabel);
    selectorLayout->addWidget(m_selectorTitle);
    selectorLayout->addLayout(scanRow);
    selectorLayout->addWidget(m_pluginListWidget, 1);
    selectorLayout->addWidget(m_loadPluginBtn);

    // ----- ページ2: エディタコンテナ（ヘッダーバー + PluginEditorWidget）-----
    m_editorContainer = new QWidget(m_editorStack);
    m_editorContainer->setStyleSheet("background-color: #1a1a2e;");
    QVBoxLayout *editorContainerLayout = new QVBoxLayout(m_editorContainer);
    editorContainerLayout->setContentsMargins(0, 0, 0, 0);
    editorContainerLayout->setSpacing(0);

    // ヘッダーバー（トラック名 + DETACHボタン）
    m_editorHeaderBar = new QWidget(m_editorContainer);
    m_editorHeaderBar->setFixedHeight(40);
    m_editorHeaderBar->setStyleSheet(
        "background-color: #252526;"
        "border-left: 1px solid #334155;"
        "border-bottom: 1px solid #2a2a3a;");
    QHBoxLayout *editorHeaderLayout = new QHBoxLayout(m_editorHeaderBar);
    editorHeaderLayout->setContentsMargins(16, 0, 12, 0);

    m_editorTrackLabel = new QLabel("", m_editorHeaderBar);
    m_editorTrackLabel->setStyleSheet(
        "font-size: 12px; font-weight: 600; color: #e2e8f0; background: transparent;");

    m_detachBtn = new QPushButton("DETACH", m_editorHeaderBar);
    m_detachBtn->setObjectName("composeDetachBtn");
    m_detachBtn->setFixedSize(70, 28);
    m_detachBtn->setCursor(Qt::PointingHandCursor);
    m_detachBtn->setStyleSheet(R"(
        #composeDetachBtn {
            border: 1px solid #fecdd3; border-radius: 4px;
            background-color: #fff5f7; color: #e11d48;
            font-size: 10px; font-weight: bold; letter-spacing: 1px; padding: 2px 8px;
        }
        #composeDetachBtn:hover { background-color: #e11d48; color: white; border-color: #e11d48; }
    )");
    connect(m_detachBtn, &QPushButton::clicked, this, &ComposeView::onDetachPluginClicked);

    editorHeaderLayout->addWidget(m_editorTrackLabel);
    editorHeaderLayout->addStretch();
    editorHeaderLayout->addWidget(m_detachBtn);

    m_pluginEditor = new PluginEditorWidget(m_editorContainer);

    editorContainerLayout->addWidget(m_editorHeaderBar);
    editorContainerLayout->addWidget(m_pluginEditor, 1);

    // スタックに追加
    m_editorStack->addWidget(m_noPluginPanel);       // index 0
    m_editorStack->addWidget(m_pluginSelectorPanel); // index 1
    m_editorStack->addWidget(m_editorContainer);     // index 2

    m_mainSplitter->addWidget(m_vSplitter);
    m_mainSplitter->addWidget(m_editorStack);
    m_mainSplitter->setStretchFactor(0, 3);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setSizes(QList<int>() << 800 << 400);

    layout->addWidget(m_mainSplitter);

    // Connections
    if (m_arrangementGrid && m_pianoRollView) {
        connect(m_arrangementGrid, &ArrangementGridWidget::clipSelected,
                m_pianoRollView, &PianoRollView::setActiveClip);
    }
    if (m_arrangementView) {
        connect(m_arrangementView, &ArrangementView::trackSelected,
                this, &ComposeView::onTrackSelected);
    }
    // 水平スクロールバーを同期
    if (m_arrangementView->horizontalScrollBar() && m_pianoRollView->horizontalScrollBar()) {
        connect(m_arrangementView->horizontalScrollBar(), &QScrollBar::valueChanged,
                m_pianoRollView->horizontalScrollBar(), &QScrollBar::setValue);
        connect(m_pianoRollView->horizontalScrollBar(), &QScrollBar::valueChanged,
                m_arrangementView->horizontalScrollBar(), &QScrollBar::setValue);
    }
}

// ---------------------------------------------------------------------------
void ComposeView::applyTheme()
{
    const Darwin::ThemeManager& tm = Darwin::ThemeManager::instance();
    const QString border   = tm.borderColor().name();
    const QString panelBg  = tm.panelBackgroundColor().name();
    const QString bg       = tm.backgroundColor().name();
    const QString text     = tm.textColor().name();
    const QString textSec  = tm.secondaryTextColor().name();
    const bool    isDark   = tm.isDarkMode();

    // スプリッターハンドル
    const QString splitterStyle =
        QString("QSplitter::handle { background-color: %1; }").arg(border);
    if (m_mainSplitter) m_mainSplitter->setStyleSheet(splitterStyle);
    if (m_vSplitter)    m_vSplitter->setStyleSheet(splitterStyle);

    // ページ0: トラック未選択パネル
    if (m_noPluginPanel)
        m_noPluginPanel->setStyleSheet(
            QString("background-color: %1; border-left: 1px solid %2;").arg(panelBg, border));
    if (m_noPluginLabel)
        m_noPluginLabel->setStyleSheet(
            QString("color: %1; font-size: 14px;").arg(textSec));

    // ページ1: プラグイン選択パネル
    if (m_pluginSelectorPanel)
        m_pluginSelectorPanel->setStyleSheet(
            QString("background-color: %1; border-left: 1px solid %2;").arg(panelBg, border));
    if (m_selectorTrackLabel)
        m_selectorTrackLabel->setStyleSheet(
            QString("font-size: 13px; font-weight: 600; color: %1;").arg(text));
    if (m_pluginListWidget) {
        const QString selected = isDark ? "#2d1a23" : "#fff0f3";
        const QString hover    = isDark ? "#2d3748" : "#f1f5f9";
        m_pluginListWidget->setStyleSheet(QString(R"(
            QListWidget { border: 1px solid %1; border-radius: 6px;
                          background-color: %2; font-size: 12px; outline: none; }
            QListWidget::item { padding: 10px 12px; border-bottom: 1px solid %1; color: %3; }
            QListWidget::item:selected { background-color: %4; color: %3; }
            QListWidget::item:hover:!selected { background-color: %5; }
        )").arg(border, bg, text, selected, hover));
    }
    if (m_loadPluginBtn) {
        const QString btnBg   = isDark ? "#334155" : "#e2e8f0";
        const QString disabledBg = isDark ? "#2a2a3a" : "#f1f5f9";
        m_loadPluginBtn->setStyleSheet(QString(R"(
            #composeAssignBtn { background-color: %1; color: %2; border: none;
                border-radius: 6px; font-size: 11px; font-weight: bold; letter-spacing: 1px; }
            #composeAssignBtn:hover { background-color: #FF3366; color: #ffffff; }
            #composeAssignBtn:disabled { background-color: %3; color: %4; }
        )").arg(btnBg, text, disabledBg, textSec));
    }

    // ページ1: スキャン画面の追加ウィジェット
    if (m_selectorTitle)
        m_selectorTitle->setStyleSheet(
            QString("font-size: 9px; font-weight: bold; color: %1; letter-spacing: 1px;").arg(textSec));
    if (m_rescanBtn)
        m_rescanBtn->setStyleSheet(QString(R"(
            #composeScanBtn {
                border: none; background-color: transparent; color: %1;
                font-size: 10px; font-weight: bold; padding: 0px 4px;
            }
            #composeScanBtn:hover { color: #FF3366; }
            #composeScanBtn:disabled { color: %2; }
        )").arg(textSec, isDark ? QString("#cbd5e1") : QString("#94a3b8")));

    // ページ2: エディタヘッダーバー
    if (m_editorHeaderBar)
        m_editorHeaderBar->setStyleSheet(
            QString("background-color: %1; border-left: 1px solid %2; border-bottom: 1px solid %2;")
            .arg(panelBg, border));
    if (m_editorTrackLabel)
        m_editorTrackLabel->setStyleSheet(
            QString("font-size: 12px; font-weight: 600; color: %1; background: transparent;").arg(text));
}

// ========== プラグイン選択パネル関連 ==========

void ComposeView::refreshPluginList()
{
    m_pluginListWidget->clear();
    if (m_scannedPlugins.isEmpty()) {
        QListWidgetItem* hint = new QListWidgetItem("No plugins found — press SCAN");
        hint->setFlags(hint->flags() & ~Qt::ItemIsSelectable);
        hint->setForeground(QColor("#94a3b8"));
        m_pluginListWidget->addItem(hint);
        return;
    }
    for (const auto& info : m_scannedPlugins) {
        QString label = info.vendor.isEmpty()
            ? info.name
            : QString("%1  ·  %2").arg(info.name, info.vendor);
        QListWidgetItem* item = new QListWidgetItem(label);
        item->setToolTip(info.path);
        m_pluginListWidget->addItem(item);
    }
}

void ComposeView::onPluginItemClicked(QListWidgetItem* item)
{
    if (!item) return;
    int row = m_pluginListWidget->row(item);
    if (row >= 0 && row < m_scannedPlugins.size()) {
        m_selectedPluginIndex = row;
        m_loadPluginBtn->setEnabled(true);
    }
}

void ComposeView::onLoadPluginClicked()
{
    if (!m_currentTrack) return;
    if (m_selectedPluginIndex < 0 || m_selectedPluginIndex >= m_scannedPlugins.size()) return;

    const VST3PluginInfo& info = m_scannedPlugins[m_selectedPluginIndex];
    m_loadPluginBtn->setEnabled(false);
    m_loadPluginBtn->setText("LOADING...");

    if (m_currentTrack->loadPlugin(info.path)) {
        m_currentTrack->setInstrumentName(info.name);
        openEditorForCurrentTrack();
    } else {
        m_loadPluginBtn->setEnabled(true);
    }
    m_loadPluginBtn->setText("ASSIGN PLUGIN");
}

void ComposeView::onRescanClicked()
{
    m_scanStatusLabel->setText("Scanning...");
    m_rescanBtn->setEnabled(false);
    m_pluginListWidget->clear();
    QListWidgetItem* scanningItem = new QListWidgetItem("Scanning...");
    scanningItem->setFlags(scanningItem->flags() & ~Qt::ItemIsSelectable);
    scanningItem->setForeground(QColor("#94a3b8"));
    m_pluginListWidget->addItem(scanningItem);
    m_selectedPluginIndex = -1;
    m_loadPluginBtn->setEnabled(false);
    m_scannedPlugins.clear();

    // 並列スキャン
    QFuture<QVector<VST3PluginInfo>> future = QtConcurrent::run([this]() {
        return m_scanner->scan(true);
    });
    QFutureWatcher<QVector<VST3PluginInfo>>* watcher =
        new QFutureWatcher<QVector<VST3PluginInfo>>(this);
    connect(watcher, &QFutureWatcher<QVector<VST3PluginInfo>>::finished, this,
            [this, watcher]() {
        m_scannedPlugins = watcher->result();
        m_scanStatusLabel->setText(QString("%1 found").arg(m_scannedPlugins.size()));
        m_rescanBtn->setEnabled(true);
        refreshPluginList();
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

// ========== エディタコンテナ関連 ==========

void ComposeView::openEditorForCurrentTrack()
{
    if (!m_currentTrack || !m_currentTrack->hasPlugin()) return;

    // ヘッダーバーにトラック名とインストゥルメント名を表示
    const QString instrName = m_currentTrack->instrumentName();
    m_editorTrackLabel->setText(
        instrName.isEmpty()
            ? m_currentTrack->name()
            : QString("%1  —  %2").arg(m_currentTrack->name(), instrName));

    m_editorStack->setCurrentWidget(m_editorContainer);

    if (!m_pluginEditor->openEditor(m_currentTrack->pluginInstance())) {
        // エディタが開けない場合はプラグイン選択パネルに戻る
        m_selectorTrackLabel->setText(m_currentTrack->name());
        m_editorStack->setCurrentWidget(m_pluginSelectorPanel);
    }
}

void ComposeView::onDetachPluginClicked()
{
    if (!m_currentTrack) return;

    // エディタを閉じる
    if (m_pluginEditor && m_pluginEditor->isEditorOpen()) {
        m_pluginEditor->closeEditor();
    }

    // プラグインをアンロード
    m_currentTrack->unloadPlugin();
    m_currentTrack->setInstrumentName(QString());

    // プラグイン選択パネルに戻る
    m_selectorTrackLabel->setText(m_currentTrack->name());
    m_selectedPluginIndex = -1;
    m_loadPluginBtn->setEnabled(false);
    refreshPluginList();
    m_editorStack->setCurrentWidget(m_pluginSelectorPanel);
}
