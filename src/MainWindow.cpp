#include "MainWindow.h"
#include "SourceView.h"
#include "ComposeView.h"
#include "MixView.h"
#include "ArrangementView.h"
#include "PianoRollView.h"
#include "TimelineWidget.h"
#include "Project.h"
#include "Track.h"
#include "Clip.h"
#include "PlaybackController.h"
#include "AudioEngine.h"
#include "RecordModeButton.h"
#include "ArrangementGridWidget.h"
#include "PianoRollGridWidget.h"
#include "common/AudioFileReader.h"
#include "commands/UndoCommands.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <QShortcut>
#include <QKeySequence>
#include <QIcon>
#include <QProgressDialog>
#include <QApplication>
#include <QToolButton>
#include "ExportProgressDialog.h"
#include <QFile>
#include <QSvgRenderer>
#include <QPainter>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QFileInfo>
#include "common/FadeHelper.h"
#include "common/FontManager.h"
#include "AudioExporter.h"
#include "ProjectLoadDialog.h"
#include "common/ThemeManager.h"
#include "BulbRaysOverlay.h"
#include <QUndoStack>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include <functional>
#include <memory>

// ─── SVGアイコンヘルパー ─────────────────────────────────────────

/**
 * @brief SVGファイルを指定色で描画した QIcon を作成する
 *
 * Qtの QIcon は SVG 内の currentColor やハードコード色を自動設定する機能がないため、
 * SVGテキスト内の色を置換してから QSvgRenderer でアイコンを生成する。
 */
static QIcon coloredSvgIcon(const QString& resourcePath, const QColor& color,
                            const QSize& size = QSize(24, 24))
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) return QIcon(resourcePath);

    QString svg = QString::fromUtf8(file.readAll());

    // ハードコードカラーと currentColor をテーマ素材色に置換
    const QString col = color.name();
    svg.replace(QLatin1String("stroke=\"#333333\""), QString("stroke=\"%1\"").arg(col));
    svg.replace(QLatin1String("fill=\"#333333\""),   QString("fill=\"%1\"").arg(col));
    svg.replace(QLatin1String("stroke=\"currentColor\""), QString("stroke=\"%1\"").arg(col));
    svg.replace(QLatin1String("fill=\"currentColor\""),   QString("fill=\"%1\"").arg(col));

    QSvgRenderer renderer(svg.toUtf8());
    QPixmap pixmap(size * 2); // 高DPI対応
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter);
    pixmap.setDevicePixelRatio(2.0); // HiDPI
    return QIcon(pixmap);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_project(nullptr)
    , m_playbackController(nullptr)
    , m_composeView(nullptr)
    , m_playBtn(nullptr)
    , m_skipPrevBtn(nullptr)
    , m_skipNextBtn(nullptr)
    , m_recordBtn(nullptr)
    , m_bpmSpinBox(nullptr)
    , m_timecodeLabel(nullptr)
    , m_undoStack(nullptr)
{
    // Undo/Redoスタックを作成
    m_undoStack = new QUndoStack(this);
    m_undoStack->setUndoLimit(100); // 100回までUndo可能に設定

    m_project = new Project("Demo Project", this);
    m_project->setBpm(128.0);

    // Create playback controller
    m_playbackController = new PlaybackController(m_project, this);

    // Theme setup
    connect(&Darwin::ThemeManager::instance(), &Darwin::ThemeManager::themeChanged, this, &MainWindow::applyGlobalStyle);

    setupUi();
    setupShortcuts();
    resize(1280, 720);
    setWindowIcon(QIcon(":/icons/darwin.png"));
    updateWindowTitle();
    
    // Connect playback signals
    connect(m_playbackController, &PlaybackController::playStateChanged,
            this, &MainWindow::onPlayStateChanged);
    connect(m_playbackController, &PlaybackController::positionChanged,
            this, &MainWindow::onPlayheadPositionChanged);
    connect(m_playbackController, &PlaybackController::recordingStateChanged,
            this, &MainWindow::onRecordingStateChanged);
    connect(m_playbackController, &PlaybackController::recordingFailed,
            this, &MainWindow::onRecordingFailed);
    connect(m_playbackController, &PlaybackController::recordingCommitted,
            this, &MainWindow::onRecordingCommitted);
    
    // プロジェクト変更時にタイトルバー更新
    connect(m_project, &Project::modified, this, &MainWindow::updateWindowTitle);
    connect(m_project, &Project::nameChanged, this, &MainWindow::updateWindowTitle);
}

void MainWindow::setupUi()
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Header
    QWidget *headerContainer = new QWidget(this);
    headerContainer->setObjectName("header");
    headerContainer->setFixedHeight(64); // h-16 = 4rem = 64px approx
    QHBoxLayout *headerLayout = new QHBoxLayout(headerContainer);
    headerLayout->setContentsMargins(32, 0, 32, 0); // px-8
    
    // Left side: Title and Nav
    QWidget *leftHeader = new QWidget(headerContainer);
    QHBoxLayout *leftHeaderLayout = new QHBoxLayout(leftHeader);
    leftHeaderLayout->setContentsMargins(0, 0, 0, 0);
    leftHeaderLayout->setSpacing(24); // smaller gap to bring title closer to menu

    QLabel *titleLabel = new QLabel("Darwin", leftHeader);
    titleLabel->setObjectName("appTitle");
    
    QWidget *navContainer = new QWidget(leftHeader);
    QHBoxLayout *navLayout = new QHBoxLayout(navContainer);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(16); // gap-4

    m_btnSource = new QPushButton("SOURCE", navContainer);
    m_btnCompose = new QPushButton("COMPOSE", navContainer);
    m_btnMix = new QPushButton("MIX", navContainer);
    
    m_btnSource->setCheckable(true);
    m_btnCompose->setCheckable(true);
    m_btnMix->setCheckable(true);
    
    // 初期画面は SOURCE
    m_btnSource->setChecked(true);

    navLayout->addWidget(m_btnSource);
    navLayout->addWidget(m_btnCompose);
    navLayout->addWidget(m_btnMix);

    leftHeaderLayout->addWidget(titleLabel);
    // Menu buttons placed immediately after the title
    setupMenuBar(leftHeaderLayout);
    leftHeaderLayout->addWidget(navContainer);
    leftHeaderLayout->addStretch();

    // Transport (Right side)
    QWidget *transportContainer = new QWidget(headerContainer);
    QHBoxLayout *transportLayout = new QHBoxLayout(transportContainer);
    transportLayout->setContentsMargins(0, 0, 0, 0);
    transportLayout->setSpacing(32); // gap-8

    // Play/BPM
    QWidget *playBpmContainer = new QWidget(transportContainer);
    QHBoxLayout *playBpmLayout = new QHBoxLayout(playBpmContainer);
    playBpmLayout->setContentsMargins(0, 0, 0, 0);
    playBpmLayout->setSpacing(24); // gap-6

    m_rewindBtn = new QPushButton(playBpmContainer);
    m_rewindBtn->setObjectName("rewindBtn");
    m_rewindBtn->setFixedSize(40, 40);
    m_rewindBtn->setIcon(QIcon(":/icons/rewind.svg"));
    m_rewindBtn->setIconSize(QSize(20, 20));
    connect(m_rewindBtn, &QPushButton::clicked, this, [this]() {
        if (m_playbackController) {
            m_playbackController->seekTo(0);
        }
    });

    // 前のフラッグへスキップ
    m_skipPrevBtn = new QPushButton(playBpmContainer);
    m_skipPrevBtn->setObjectName("skipPrevBtn");
    m_skipPrevBtn->setFixedSize(40, 40);
    m_skipPrevBtn->setIcon(QIcon(":/icons/skip-prev.svg"));
    m_skipPrevBtn->setIconSize(QSize(20, 20));
    m_skipPrevBtn->setToolTip("前のフラッグへ移動");
    connect(m_skipPrevBtn, &QPushButton::clicked, this, [this]() {
        if (m_playbackController && m_project) {
            qint64 currentTick = m_project->playheadPosition();
            qint64 prevFlagTick = m_project->prevFlag(currentTick);
            if (prevFlagTick >= 0) {
                m_playbackController->seekTo(prevFlagTick);
            }
        }
    });

    m_playBtn = new QPushButton(playBpmContainer);
    m_playBtn->setObjectName("playBtn");
    m_playBtn->setFixedSize(40, 40);
    m_playBtn->setIcon(QIcon(":/icons/play.svg"));
    m_playBtn->setIconSize(QSize(20, 20));
    connect(m_playBtn, &QPushButton::clicked, this, &MainWindow::onPlayButtonClicked);

    // 次のフラッグへスキップ
    m_skipNextBtn = new QPushButton(playBpmContainer);
    m_skipNextBtn->setObjectName("skipNextBtn");
    m_skipNextBtn->setFixedSize(40, 40);
    m_skipNextBtn->setIcon(QIcon(":/icons/skip-next.svg"));
    m_skipNextBtn->setIconSize(QSize(20, 20));
    m_skipNextBtn->setToolTip("次のフラッグへ移動");
    connect(m_skipNextBtn, &QPushButton::clicked, this, [this]() {
        if (m_playbackController && m_project) {
            qint64 currentTick = m_project->playheadPosition();
            qint64 nextFlagTick = m_project->nextFlag(currentTick);
            if (nextFlagTick >= 0) {
                m_playbackController->seekTo(nextFlagTick);
            }
        }
    });

    m_recordBtn = new RecordModeButton(playBpmContainer);
    m_recordBtn->setObjectName("recordBtn");
    m_recordBtn->setToolTip("音声録音");
    connect(m_recordBtn, &RecordModeButton::clicked,
            this, &MainWindow::onRecordButtonClicked);
    connect(m_recordBtn, &RecordModeButton::modeChanged, this,
            [this](RecordModeButton::Mode mode) {
        if (!m_recordBtn) {
            return;
        }
        m_recordBtn->setToolTip(mode == RecordModeButton::Mode::Midi
            ? QStringLiteral("MIDI録音")
            : QStringLiteral("音声録音"));
    });

    // BPM SpinBox
    m_bpmSpinBox = new QDoubleSpinBox(playBpmContainer);
    m_bpmSpinBox->setObjectName("bpmSpinBox");
    m_bpmSpinBox->setRange(20.0, 999.0);
    m_bpmSpinBox->setDecimals(2);
    m_bpmSpinBox->setSuffix(" BPM");
    m_bpmSpinBox->setValue(m_project->bpm());
    m_bpmSpinBox->setButtonSymbols(QDoubleSpinBox::NoButtons);
    m_bpmSpinBox->setFixedWidth(100);
    m_bpmSpinBox->setAlignment(Qt::AlignCenter);
    connect(m_bpmSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MainWindow::onBpmChanged);
    
    playBpmLayout->addWidget(m_rewindBtn);
    playBpmLayout->addWidget(m_skipPrevBtn);
    playBpmLayout->addWidget(m_playBtn);
    playBpmLayout->addWidget(m_skipNextBtn);
    playBpmLayout->addWidget(m_recordBtn);
    playBpmLayout->addWidget(m_bpmSpinBox);

    // Divider
    QFrame *divider = new QFrame(transportContainer);
    divider->setFrameShape(QFrame::VLine);
    divider->setObjectName("headerDivider");
    divider->setFixedSize(1, 24);

    // Timecode
    m_timecodeLabel = new QLabel("001:01:000", transportContainer);
    m_timecodeLabel->setObjectName("timecodeLabel");

    transportLayout->addWidget(playBpmContainer);
    transportLayout->addWidget(divider);
    transportLayout->addWidget(m_timecodeLabel);

    headerLayout->addWidget(leftHeader);
    headerLayout->addStretch();
    headerLayout->addWidget(transportContainer);

    // Main Content
    m_stackedWidget = new QStackedWidget(this);
    m_sourceView = new SourceView(this);
    m_stackedWidget->addWidget(m_sourceView);
    
    ComposeView* composeView = new ComposeView(this);
    m_composeView = composeView;
    m_stackedWidget->addWidget(composeView);
    
    // プロジェクトをComposeView全体に注入（ArrangementView + PianoRollView両方に伝播）
    composeView->setProject(m_project);
    composeView->setUndoStack(m_undoStack);
    
    MixView* mixView = new MixView(this);
    mixView->setProject(m_project);
    mixView->setPlaybackController(m_playbackController);
    m_stackedWidget->addWidget(mixView);
    m_stackedWidget->setCurrentIndex(0); // 初期画面は SOURCE

    mainLayout->addWidget(headerContainer);
    mainLayout->addWidget(m_stackedWidget);
    
    // Connect playhead to grid widgets
    if (composeView->arrangementGrid()) {
        connect(m_playbackController, &PlaybackController::positionChanged,
                composeView->arrangementGrid(), &ArrangementGridWidget::setPlayheadPosition);
        connect(m_playbackController, &PlaybackController::playStateChanged,
                composeView->arrangementGrid(), &ArrangementGridWidget::setPlaying);
        connect(composeView->arrangementGrid(), &ArrangementGridWidget::requestSeek,
                m_playbackController, &PlaybackController::seekTo);
    }
    if (composeView->pianoRollGrid()) {
        connect(m_playbackController, &PlaybackController::positionChanged,
                composeView->pianoRollGrid(), &PianoRollGridWidget::setPlayheadPosition);
        connect(m_playbackController, &PlaybackController::playStateChanged,
                composeView->pianoRollGrid(), &PianoRollGridWidget::setPlaying);
        connect(composeView->pianoRollGrid(), &PianoRollGridWidget::requestSeek,
                m_playbackController, &PlaybackController::seekTo);
    }
    if (composeView->arrangementView() && composeView->arrangementView()->timelineWidget()) {
        connect(m_playbackController, &PlaybackController::playStateChanged,
                composeView->arrangementView()->timelineWidget(), &TimelineWidget::setPlaying);
        connect(composeView->arrangementView()->timelineWidget(), &TimelineWidget::requestSeek,
                m_playbackController, &PlaybackController::seekTo);
    }
    if (composeView->arrangementView()) {
        connect(composeView->arrangementView(), &ArrangementView::trackSelected,
                m_playbackController, &PlaybackController::setMidiMonitorTrack);
        m_playbackController->setMidiMonitorTrack(
            composeView->arrangementView()->selectedTrack());
    }
    
    // Connect SourceView to Project
    connect(m_sourceView, &SourceView::loadInstrumentRequested, this, [this](const QString& instrumentName, const QString& path){
        // トラック追加はUndo/Redo対象外とする（ユーザーリクエスト）
        Track* newTrack = m_project->addTrack(instrumentName);
        if (!newTrack) {
            m_sourceView->onPluginLoaded(false, "Failed to create track");
            return;
        }

        // VST3 DLL loading must run on the main thread (COM / window message pump).

        // VST3 DLL loading must run on the main thread (COM / window message pump).
        // Use a short delay so the overlay animation starts before the blocking load.
        QPointer<Track> trackPtr(newTrack);
        QTimer::singleShot(250, this, [this, trackPtr, path](){
            if (trackPtr.isNull()) {
                m_sourceView->onPluginLoaded(false, "Track was deleted");
                return;
            }
            bool ok = trackPtr->loadPlugin(path);
            m_sourceView->onPluginLoaded(ok, ok ? QString() : "Failed to load plugin");
        });
    });

    // Connections
    connect(m_btnSource, &QPushButton::clicked, [this](){ switchMode(0); });
    connect(m_btnCompose, &QPushButton::clicked, [this](){ switchMode(1); });
    connect(m_btnMix, &QPushButton::clicked, [this](){ switchMode(2); });

    // Global Stylesheet の初期適用
    applyGlobalStyle();
}

void MainWindow::applyGlobalStyle()
{
    const Darwin::ThemeManager& tm = Darwin::ThemeManager::instance();
    bool isDark = tm.isDarkMode();
    
    QString bgStr = tm.backgroundColor().name();
    QString textStr = tm.textColor().name();
    QString textSecStr = tm.secondaryTextColor().name();
    QString borderStr = tm.borderColor().name();
    QString panelBgStr = tm.panelBackgroundColor().name();
    
    // Theme-dependent generic button styles
    QString btnHoverBgStr = isDark ? "#334155" : "#f8fafc";
    QString btnBorderStr = isDark ? "#334155" : "#e2e8f0";
    QString btnBorderHoverStr = isDark ? "#475569" : "#cbd5e1";
    QString inputBgStr = isDark ? "#1e1e1e" : "#ffffff";
    QString menuBgStr = isDark ? "#1e293b" : "#ffffff";
    QString menuSelectedStr = isDark ? "#334155" : "#f1f5f9";
    const QString uiFontCss = Darwin::FontManager::uiFontCss();
    const QString monoFontCss = Darwin::FontManager::monoFontCss();

    QString css = QString(
        "QMainWindow, QDialog { background-color: %1; }"
        "QMessageBox, QProgressDialog { background-color: %1; }"
        "QLabel { color: %2; }"
        // Header
        "#header { background-color: %1; border-bottom: 1px solid %4; }"
        "#appTitle { font-family: %11; font-weight: 300; font-size: 20px; letter-spacing: 2px; text-transform: uppercase; color: %2; }"
        
        // Navigation Buttons
        "QPushButton { border: none; background: none; font-family: %11; font-weight: 700; font-size: 11px; letter-spacing: 1px; color: %3; padding: 4px 12px; border-bottom: 2px solid transparent; text-transform: uppercase; }"
        "QPushButton:checked { color: #FF3366; border-bottom-color: #FF3366; }"
        "QPushButton:hover { color: %2; }"
        
        // Transport
        "#playBtn, #rewindBtn, #skipPrevBtn, #skipNextBtn { border: 1px solid %6; border-radius: 16px; color: %2; font-size: 14px; background-color: transparent; padding: 4px; }"
        "#playBtn:hover, #rewindBtn:hover, #skipPrevBtn:hover, #skipNextBtn:hover { border-color: %7; background-color: %5; color: #FF3366; }"
        "#bpmSpinBox { font-family: %11; font-size: 11px; font-weight: 700; color: %2; border: 1px solid %6; border-radius: 4px; padding: 4px 8px; background-color: %8; }"
        "#bpmSpinBox:focus { border-color: #FF3366; }"
        "#timecodeLabel { font-family: %12; font-size: 11px; color: %3; }"
        "#headerDivider { background-color: %4; border: none; }"

        // ScrollBars
        "QScrollBar:vertical { border: none; background: transparent; width: 8px; margin: 0px; }"
        "QScrollBar::handle:vertical { background: %4; min-height: 20px; border-radius: 4px; margin: 2px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
        "QScrollBar:horizontal { border: none; background: transparent; height: 8px; margin: 0px; }"
        "QScrollBar::handle:horizontal { background: %4; min-width: 20px; border-radius: 4px; margin: 2px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
        
        // Menu / ToolButtons (ハンバーガーメニュー等)
        "QMenu { background: %9; border: 1px solid %4; border-radius: 4px; padding: 4px 0px; font-family: %11; }"
        "QMenu::item { padding: 6px 32px 6px 24px; font-size: 12px; color: %2; }"
        "QMenu::item:selected { background: %10; color: #FF3366; }"
        "QMenu::separator { height: 1px; background: %4; margin: 4px 8px; }"
        "QToolButton { border: none; padding: 2px; border-radius: 4px; } QToolButton::menu-indicator { image: none; } QToolButton:hover { background-color: %10; }"
    ).arg(bgStr, textStr, textSecStr, borderStr, btnHoverBgStr, btnBorderStr, btnBorderHoverStr, inputBgStr, menuBgStr, menuSelectedStr)
     .arg(uiFontCss)
     .arg(monoFontCss);

    qApp->setStyleSheet(css);

    // ─── SVGアイコンをテーマ色で再レンダリング ───────────────────────
    const QColor iconColor = tm.textColor();
    const bool isPlaying = m_playbackController && m_playbackController->isPlaying();

    if (m_rewindBtn)
        m_rewindBtn->setIcon(coloredSvgIcon(":/icons/rewind.svg", iconColor, QSize(20,20)));
    if (m_skipPrevBtn)
        m_skipPrevBtn->setIcon(coloredSvgIcon(":/icons/skip-prev.svg", iconColor, QSize(20,20)));
    if (m_playBtn)
        m_playBtn->setIcon(coloredSvgIcon(
            isPlaying ? ":/icons/pause.svg" : ":/icons/play.svg", iconColor, QSize(20,20)));
    if (m_skipNextBtn)
        m_skipNextBtn->setIcon(coloredSvgIcon(":/icons/skip-next.svg", iconColor, QSize(20,20)));
    if (m_themeToggleBtn)
        m_themeToggleBtn->setIcon(coloredSvgIcon(":/icons/bulb.svg", iconColor, QSize(16,16)));

    // メニューツールボタン
    if (m_btnMenu) m_btnMenu->setIcon(coloredSvgIcon(":/icons/menu.svg",   iconColor, QSize(14,14)));
    if (m_btnOpen) m_btnOpen->setIcon(coloredSvgIcon(":/icons/folder.svg", iconColor, QSize(14,14)));
    if (m_btnSave) m_btnSave->setIcon(coloredSvgIcon(":/icons/save.svg",   iconColor, QSize(14,14)));

    // メニューアクション
    if (m_newAction)    m_newAction->setIcon(coloredSvgIcon(":/icons/new.svg",    iconColor, QSize(16,16)));
    if (m_openAction)   m_openAction->setIcon(coloredSvgIcon(":/icons/folder.svg", iconColor, QSize(16,16)));
    if (m_saveAction)   m_saveAction->setIcon(coloredSvgIcon(":/icons/save.svg",   iconColor, QSize(16,16)));
    if (m_exportAction) m_exportAction->setIcon(coloredSvgIcon(":/icons/export.svg", iconColor, QSize(16,16)));
}

void MainWindow::switchMode(int index)
{
    int oldIndex = m_stackedWidget->currentIndex();
    m_stackedWidget->setCurrentIndex(index);
    m_btnSource->setChecked(index == 0);
    m_btnCompose->setChecked(index == 1);
    m_btnMix->setChecked(index == 2);

    // 切替アニメーション（同じ画面でない場合のみ）
    if (oldIndex != index) {
        QWidget* incoming = m_stackedWidget->widget(index);
        if (incoming) {
            FadeHelper::fadeIn(incoming, 300);
        }
    }
}

void MainWindow::onPlayButtonClicked()
{
    if (m_playbackController) {
        m_playbackController->togglePlayPause();
    }
}

void MainWindow::onRecordButtonClicked()
{
    if (!m_playbackController || !m_recordBtn) {
        return;
    }

    if (m_playbackController->isRecording()) {
        if (!m_playbackController->stopRecording()) {
            clearPendingRecordingUndoState();
        }
        return;
    }

    const auto mode = (m_recordBtn->mode() == RecordModeButton::Mode::Midi)
        ? PlaybackController::RecordingMode::Midi
        : PlaybackController::RecordingMode::Audio;

    // 1テイクごとに録音結果を1つのUndo単位へまとめるため、
    // 開始前に前回ぶんの一時状態を必ず捨てておく。
    clearPendingRecordingUndoState();

    bool createdTrack = false;
    Track* targetTrack = resolveRecordingTarget(
        mode == PlaybackController::RecordingMode::Midi,
        &createdTrack);
    if (!targetTrack) {
        QMessageBox::warning(this, "録音エラー", "録音先トラックを用意できませんでした。");
        return;
    }

    if (!m_playbackController->startRecording(targetTrack, mode)) {
        if (createdTrack && m_project) {
            // 録音開始前提で自動作成したトラックなので、
            // 実際に録音へ入れなかった場合はモデルを元に戻す。
            m_project->removeTrack(targetTrack);
        }
        clearPendingRecordingUndoState();
        return;
    }

    // 録音完了時に「新規トラック作成を含むテイクだったか」をUndoへ反映するため保持する。
    m_pendingRecordingTrack = targetTrack;
    m_pendingRecordingCreatedTrack = createdTrack;
}

void MainWindow::onPlayStateChanged(bool isPlaying)
{
    if (m_playBtn) {
        const QColor iconColor = Darwin::ThemeManager::instance().textColor();
        m_playBtn->setIcon(coloredSvgIcon(
            isPlaying ? ":/icons/pause.svg" : ":/icons/play.svg", iconColor, QSize(20,20)));
    }
}

void MainWindow::onRecordingStateChanged(bool isRecording)
{
    if (m_recordBtn) {
        m_recordBtn->setRecording(isRecording);
    }

    if (!isRecording) {
        QTimer::singleShot(0, this, [this]() {
            if (!m_playbackController || !m_playbackController->isRecording()) {
                clearPendingRecordingUndoState();
            }
        });
    }
}

void MainWindow::onRecordingFailed(const QString& message)
{
    if (m_recordBtn) {
        m_recordBtn->setRecording(false);
    }
    clearPendingRecordingUndoState();
    QMessageBox::warning(this, "録音エラー", message);
}

void MainWindow::onRecordingCommitted(Track* track, Clip* clip)
{
    if (!m_undoStack || !track || !clip) {
        clearPendingRecordingUndoState();
        return;
    }

    const bool createdTrack =
        m_pendingRecordingCreatedTrack &&
        !m_pendingRecordingTrack.isNull() &&
        m_pendingRecordingTrack == track;

    // 録音時点ではトラック/クリップ実体が既にモデルへ追加済みなので、
    // その実体を Adopt*Command で Undo スタックへ採用する。
    m_undoStack->beginMacro(createdTrack ? "Record Track" : "Record Clip");
    if (createdTrack && m_project) {
        m_undoStack->push(new AdoptTrackCommand(m_project, track));
    }
    m_undoStack->push(new AdoptClipCommand(track, clip));
    m_undoStack->endMacro();

    clearPendingRecordingUndoState();
}

void MainWindow::onPlayheadPositionChanged(qint64 tickPosition)
{
    updateTimecode(tickPosition);
}

void MainWindow::updateTimecode(qint64 tickPosition)
{
    if (!m_project || !m_timecodeLabel) {
        return;
    }
    
    // Convert ticks to bars:beats:ticks format
    // TICKS_PER_BEAT = 480, 4 beats per bar
    const int ticksPerBeat = Project::TICKS_PER_BEAT;
    const int beatsPerBar = 4;
    const int ticksPerBar = ticksPerBeat * beatsPerBar;
    
    int bars = static_cast<int>(tickPosition / ticksPerBar) + 1;
    int beats = static_cast<int>((tickPosition % ticksPerBar) / ticksPerBeat) + 1;
    int ticks = static_cast<int>(tickPosition % ticksPerBeat);
    
    QString timecode = QString("%1:%2:%3")
        .arg(bars, 3, 10, QChar('0'))
        .arg(beats, 2, 10, QChar('0'))
        .arg(ticks, 3, 10, QChar('0'));
    
    m_timecodeLabel->setText(timecode);
}

void MainWindow::onBpmChanged(double bpm)
{
    if (m_project) {
        m_project->setBpm(bpm);
        qDebug() << "BPM changed to:" << bpm << "(Project BPM:" << m_project->bpm() << ")";
    }
}

void MainWindow::clearPendingRecordingUndoState()
{
    m_pendingRecordingTrack.clear();
    m_pendingRecordingCreatedTrack = false;
}

Track* MainWindow::resolveRecordingTarget(bool midiMode, bool* createdTrack)
{
    if (createdTrack) {
        *createdTrack = false;
    }

    if (!m_project) {
        return nullptr;
    }

    Track* selectedTrack = nullptr;
    if (m_composeView && m_composeView->arrangementView()) {
        selectedTrack = m_composeView->arrangementView()->selectedTrack();
    }

    auto isRecordable = [](Track* track) {
        return track && !track->isFolder();
    };

    // 優先順位:
    // 1. 現在選択中の通常トラック
    // 2. 既存の最初の通常トラック
    // 3. 見つからなければ録音用トラックを自動作成
    if (isRecordable(selectedTrack)) {
        return selectedTrack;
    }

    for (Track* track : m_project->tracks()) {
        if (isRecordable(track)) {
            if (m_composeView && m_composeView->arrangementView()) {
                m_composeView->arrangementView()->selectTrack(track);
            }
            return track;
        }
    }

    Track* autoCreatedTrack = m_project->addTrack(
        midiMode
            ? QStringLiteral("MIDI Track")
            : QStringLiteral("Audio Track"));
    if (autoCreatedTrack) {
        if (createdTrack) {
            *createdTrack = true;
        }
        if (m_composeView && m_composeView->arrangementView()) {
            m_composeView->arrangementView()->selectTrack(autoCreatedTrack);
        }
    }
    return autoCreatedTrack;
}

// ===== メニューバー・ショートカット =====

void MainWindow::setupMenuBar(QHBoxLayout* parentLayout)
{
    // ツールバー/メニューアイコン用のコンテナ
    QWidget* menuContainer = new QWidget(this);
    QHBoxLayout* menuLayout = new QHBoxLayout(menuContainer);
    menuLayout->setContentsMargins(0, 0, 0, 0);
    menuLayout->setSpacing(2);

    // ハンバーガーメニュー
    m_btnMenu = new QToolButton(menuContainer);
    m_btnMenu->setIconSize(QSize(14, 14));
    m_btnMenu->setFixedSize(26, 26);
    m_btnMenu->setPopupMode(QToolButton::InstantPopup);

    QMenu* mainMenu = new QMenu(m_btnMenu);
    auto createMenuAction = [this, mainMenu](const QString& text,
                                             const QKeySequence& shortcut) -> QAction* {
        QAction* action = new QAction(text, this);
        action->setShortcut(shortcut);
        action->setShortcutContext(Qt::WindowShortcut);
        action->setShortcutVisibleInContextMenu(true);
        // ポップアップ表示用のメニューと、実際にショートカットを受けるメインウィンドウの両方へ登録する
        addAction(action);
        mainMenu->addAction(action);
        return action;
    };

    // ======== fileTool ========
    m_newAction  = createMenuAction("New Project", QKeySequence::New);
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newProject);

    m_openAction = createMenuAction("Open...", QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openProject);

    mainMenu->addSeparator();

    m_saveAction = createMenuAction("Save", QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveProject);

    QAction* saveAsAction = createMenuAction("Save As...",
                                             QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveProjectAs);

    mainMenu->addSeparator();

    m_exportAction = createMenuAction("Export Audio...",
                                      QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::exportAudio);

    mainMenu->addSeparator();

    // ======== editTool ========
    QAction* undoAction = m_undoStack->createUndoAction(this, "Undo");
    undoAction->setShortcut(QKeySequence::Undo);
    undoAction->setShortcutContext(Qt::WindowShortcut);
    undoAction->setShortcutVisibleInContextMenu(true);
    addAction(undoAction);
    mainMenu->addAction(undoAction);

    QAction* redoAction = m_undoStack->createRedoAction(this, "Redo");
    redoAction->setShortcut(QKeySequence::Redo);
    redoAction->setShortcutContext(Qt::WindowShortcut);
    redoAction->setShortcutVisibleInContextMenu(true);
    addAction(redoAction);
    mainMenu->addAction(redoAction);

    m_btnMenu->setMenu(mainMenu);

    // クイックアクセスボタン（開く・保存）
    m_btnOpen = new QToolButton(menuContainer);
    m_btnOpen->setIconSize(QSize(14, 14));
    m_btnOpen->setFixedSize(26, 26);
    m_btnOpen->setToolTip("Open Project");
    connect(m_btnOpen, &QToolButton::clicked, this, &MainWindow::openProject);

    m_btnSave = new QToolButton(menuContainer);
    m_btnSave->setIconSize(QSize(14, 14));
    m_btnSave->setFixedSize(26, 26);
    m_btnSave->setToolTip("Save Project");
    connect(m_btnSave, &QToolButton::clicked, this, &MainWindow::saveProject);

    // テーマ切り替えボタン + 放射線オーバーレイ
    // オーバーレイがボタン境界外に描画できるよう 52x52 のラッパーに収める
    QWidget* themeWrapper = new QWidget(menuContainer);
    themeWrapper->setFixedSize(38, 38);
    themeWrapper->setAttribute(Qt::WA_TransparentForMouseEvents, false);

    m_themeToggleBtn = new QToolButton(themeWrapper);
    m_themeToggleBtn->setObjectName("themeToggleBtn");
    m_themeToggleBtn->setIconSize(QSize(16, 16));
    m_themeToggleBtn->setFixedSize(26, 26);
    m_themeToggleBtn->move(6, 6); // ラッパー中央
    m_themeToggleBtn->setToolTip("テーマ切り替え");
    connect(m_themeToggleBtn, &QToolButton::clicked, this, [this]() {
        // ダークモード→点灯アニメーション→ライトモード切替
        // ライトモード→消灯アニメーション→ダークモード切替
        playBulbAnimation(Darwin::ThemeManager::instance().isDarkMode());
    });

    // 放射線オーバーレイ（ラッパーと同サイズ、マウス透過）
    m_raysOverlay = new BulbRaysOverlay(themeWrapper);
    m_raysOverlay->move(0, 0);
    m_raysOverlay->raise();
    // 起動時にテーマが既にライトモードなら線を表示
    m_raysOverlay->setProgress(Darwin::ThemeManager::instance().isDarkMode() ? 0.0 : 1.0);

    menuLayout->addWidget(m_btnMenu);
    menuLayout->addWidget(m_btnOpen);
    menuLayout->addWidget(m_btnSave);
    menuLayout->addWidget(themeWrapper);

    // 親レイアウトに追加
    parentLayout->addWidget(menuContainer);
}

void MainWindow::setupShortcuts()
{
    // スペースキーで再生/停止
    auto* playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(playShortcut, &QShortcut::activated, this, &MainWindow::onPlayButtonClicked);
}

void MainWindow::updateWindowTitle()
{
    QString title = "Darwin";
    if (m_project) {
        QString fp = m_project->currentFilePath();
        if (!fp.isEmpty()) {
            QFileInfo fi(fp);
            title += " - " + fi.completeBaseName();
        } else {
            title += " - " + m_project->name();
        }
    }
    setWindowTitle(title);
}

// ===== ファイル操作 =====

void MainWindow::newProject()
{
    ++m_projectLoadGeneration;

    if (m_playbackController && m_playbackController->isRecording()) {
        m_playbackController->stopRecording();
    }

    // 再生を停止
    if (m_playbackController) {
        m_playbackController->stop();
    }

    // 既存プロジェクトをクリア
    m_project->clearTracks();
    m_project->setName("Untitled");
    m_project->setBpm(Project::DEFAULT_BPM);
    m_project->setPlayheadPosition(0);
    m_project->setFilePath("");

    if (m_bpmSpinBox) {
        m_bpmSpinBox->setValue(m_project->bpm());
    }

    m_undoStack->clear();
    updateWindowTitle();
}

void MainWindow::openProject()
{
    ++m_projectLoadGeneration;
    const quint64 loadGeneration = m_projectLoadGeneration;

    // デフォルトでProjectフォルダを開く
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                         + "/Darwin/Projects";
    QString filePath = QFileDialog::getOpenFileName(
        this, "Open Project", defaultDir,
        "Darwin Project (*.darwin);;JSON Files (*.json);;All Files (*)");

    if (filePath.isEmpty()) return;

    if (m_playbackController && m_playbackController->isRecording()) {
        m_playbackController->stopRecording();
    }

    // 再生を停止
    if (m_playbackController) {
        m_playbackController->stop();
    }

    // プロジェクト名をファイル名から取得
    QString displayName = QFileInfo(filePath).completeBaseName();

    // ローディングポップアップを表示
    auto* dlg = new ProjectLoadDialog(displayName, this);

    struct LoadProjectResult {
        bool ok = false;
        QString error;
        QJsonObject json;
    };

    // バックグラウンドでは「ファイルI/O + JSONパース」のみを行う
    auto* watcher = new QFutureWatcher<LoadProjectResult>(this);
    connect(watcher, &QFutureWatcher<LoadProjectResult>::finished, this, [this, dlg, watcher, filePath, loadGeneration]() {
        const LoadProjectResult result = watcher->result();
        watcher->deleteLater();
        QPointer<ProjectLoadDialog> safeDlg(dlg);

        if (loadGeneration != m_projectLoadGeneration) {
            if (safeDlg) safeDlg->close();
            return;
        }

        if (!result.ok || !m_project->fromJson(result.json, true, true)) {
            const QString reason = result.error.isEmpty()
                ? QStringLiteral("プロジェクトファイルの\n読み込みに失敗しました")
                : result.error;
            if (safeDlg) {
                safeDlg->showFailure(reason);
            }
            return;
        }

        m_project->setFilePath(filePath);
        if (m_bpmSpinBox) {
            m_bpmSpinBox->setValue(m_project->bpm());
        }
        m_undoStack->clear();
        updateWindowTitle();

        struct AudioClipRestoreRequest {
            QPointer<Clip> clip;
            QString filePath;
        };

        QVector<AudioClipRestoreRequest> audioRestoreQueue;
        auto collectAudioRestoreRequests = [&audioRestoreQueue](Track* track) {
            if (!track) {
                return;
            }

            for (Clip* clip : track->clips()) {
                if (!clip || !clip->isAudioClip() || clip->audioFilePath().isEmpty()) {
                    continue;
                }
                if (!clip->audioSamplesL().isEmpty()) {
                    continue;
                }
                audioRestoreQueue.push_back({clip, clip->audioFilePath()});
            }
        };
        collectAudioRestoreRequests(m_project->masterTrack());
        for (Track* track : m_project->tracks()) {
            collectAudioRestoreRequests(track);
        }

        QList<QPointer<Track>> restoreQueue;
        if (m_project->masterTrack()) {
            restoreQueue.append(m_project->masterTrack());
        }
        for (Track* track : m_project->tracks()) {
            restoreQueue.append(track);
        }

        auto pendingAudioLoads = std::make_shared<int>(audioRestoreQueue.size());
        auto deferredRestoreFinished = std::make_shared<bool>(false);
        auto finalizeLoad = std::make_shared<std::function<void()>>();
        *finalizeLoad = [this, safeDlg, loadGeneration, pendingAudioLoads, deferredRestoreFinished]() {
            if (loadGeneration != m_projectLoadGeneration) {
                if (safeDlg) {
                    safeDlg->close();
                }
                return;
            }

            if (!*deferredRestoreFinished || *pendingAudioLoads > 0) {
                return;
            }

            if (safeDlg) {
                safeDlg->showSuccess(m_project->trackCount());
            }
        };

        for (const AudioClipRestoreRequest& request : audioRestoreQueue) {
            auto* audioWatcher = new QFutureWatcher<AudioFileData>(this);
            connect(audioWatcher, &QFutureWatcher<AudioFileData>::finished, this,
                    [this, audioWatcher, request, loadGeneration, pendingAudioLoads, finalizeLoad]() {
                const AudioFileData audioData = audioWatcher->result();
                audioWatcher->deleteLater();

                if (loadGeneration != m_projectLoadGeneration) {
                    (*finalizeLoad)();
                    return;
                }

                if (request.clip) {
                    if (audioData.valid) {
                        request.clip->setAudioData(audioData.samplesL, audioData.samplesR,
                                                   audioData.sampleRate, request.filePath,
                                                   audioData.waveformPreview);
                    } else {
                        qWarning() << "Project load: オーディオファイルの再読み込みに失敗:"
                                   << request.filePath << audioData.errorMessage;
                    }
                }

                if (*pendingAudioLoads > 0) {
                    --(*pendingAudioLoads);
                }
                (*finalizeLoad)();
            });
            audioWatcher->setFuture(QtConcurrent::run([filePath = request.filePath]() {
                return AudioFileReader::readFile(filePath);
            }));
        }

        auto index = std::make_shared<int>(0);
        auto step = std::make_shared<std::function<void()>>();
        *step = [this, safeDlg, restoreQueue, index, step, loadGeneration,
                 deferredRestoreFinished, finalizeLoad]() {
            if (loadGeneration != m_projectLoadGeneration) {
                if (safeDlg) safeDlg->close();
                return;
            }

            QElapsedTimer budget;
            budget.start();

            while (*index < restoreQueue.size()) {
                Track* track = restoreQueue.at(*index).data();
                ++(*index);

                if (track && track->hasDeferredPluginRestore()) {
                    track->restoreDeferredPlugins();
                }

                if (budget.elapsed() >= 8) {
                    break;
                }
            }

            if (*index < restoreQueue.size()) {
                QTimer::singleShot(0, this, [step]() { (*step)(); });
                return;
            }

            *deferredRestoreFinished = true;
            (*finalizeLoad)();
        };

        QTimer::singleShot(0, this, [step]() { (*step)(); });
    });
    watcher->setFuture(QtConcurrent::run([filePath]() -> LoadProjectResult {
        LoadProjectResult result;

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            result.error = QStringLiteral("プロジェクトファイルを\n開けませんでした");
            return result;
        }

        const QByteArray data = file.readAll();
        file.close();

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            result.error = QStringLiteral("プロジェクトJSONの\n解析に失敗しました");
            return result;
        }

        result.ok = true;
        result.json = doc.object();
        return result;
    }));

    dlg->exec();
}

void MainWindow::saveProject()
{
    if (m_project->currentFilePath().isEmpty()) {
        saveProjectAs();
        return;
    }

    if (!m_project->saveToFile(m_project->currentFilePath())) {
        QMessageBox::warning(this, "Error", "プロジェクトの保存に失敗しました。");
    }
    updateWindowTitle();
}

void MainWindow::saveProjectAs()
{
    // デフォルトでProjectフォルダを開く
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                         + "/Darwin/Projects";
    QDir().mkpath(defaultDir);
    QString defaultPath = defaultDir + "/" + m_project->name() + ".darwin";

    QString filePath = QFileDialog::getSaveFileName(
        this, "Save Project", defaultPath,
        "Darwin Project (*.darwin);;JSON Files (*.json);;All Files (*)");

    if (filePath.isEmpty()) return;

    if (m_project->saveToFile(filePath)) {
        updateWindowTitle();
    } else {
        QMessageBox::warning(this, "Error", "プロジェクトの保存に失敗しました。");
    }
}

void MainWindow::exportAudio()
{
    if (m_playbackController && m_playbackController->isRecording()) {
        m_playbackController->stopRecording();
    }

    // エクスポート先ファイルを選択
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                         + "/Darwin/Projects";
    QDir().mkpath(defaultDir);

    QString defaultName;
    if (m_project) {
        QString fp = m_project->currentFilePath();
        if (!fp.isEmpty()) {
            defaultName = QFileInfo(fp).completeBaseName();
        } else {
            defaultName = m_project->name();
        }
    } else {
        defaultName = "Untitled";
    }
    
    QString defaultPath = defaultDir + "/" + defaultName + ".wav";

    QString filePath = QFileDialog::getSaveFileName(
        this, "オーディオエクスポート", defaultPath,
        "WAV ファイル (*.wav);;全てのファイル (*)");
    if (filePath.isEmpty()) return;

    // WAVの拡張子を付与
    if (!filePath.endsWith(".wav", Qt::CaseInsensitive)) {
        filePath += ".wav";
    }

    // 進捗ダイアログをヒープに確保
    QString rangeInfo;
    if (m_project->exportStartBar() >= 0 && m_project->exportEndBar() > m_project->exportStartBar()) {
        rangeInfo = QString("小節 %1 〜 %2 をエクスポート中...")
            .arg(m_project->exportStartBar() + 1.0, 0, 'f', 1)
            .arg(m_project->exportEndBar(), 0, 'f', 1);
    } else {
        rangeInfo = "オーディオをエクスポート中...";
    }
    
    auto* progressDlg = new ExportProgressDialog(rangeInfo, this);
    progressDlg->show();

    // エクスポート中はAudioEngine・タイマーを停止し、プラグインへの競合アクセスを防ぐ
    m_playbackController->suspendForExport();

    // デバイスのネイティブサンプルレートを使用してプラグインの再初期化を回避する
    // （44100固定にするとKontakt等が「サンプルレートが変わった」ダイアログを表示する）
    double exportSampleRate = 44100.0;
    if (m_playbackController && m_playbackController->audioEngine()) {
        double deviceRate = m_playbackController->audioEngine()->sampleRate();
        if (deviceRate > 0) exportSampleRate = deviceRate;
    }

    auto* exporter = new AudioExporter(this);
    connect(exporter, &AudioExporter::progressChanged, this, [progressDlg](double progress) {
        if (progressDlg) {
            progressDlg->setProgress(progress);
        }
    });

    // バックグラウンドでエクスポートを実行
    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, exporter, progressDlg, filePath]() {
        bool success = watcher->result();
        
        watcher->deleteLater();
        exporter->deleteLater();
        
        if (progressDlg) {
            progressDlg->accept(); // close dialog
        }

        // デッドロック回避: AudioEngine・タイマーの再開前に完了メッセージを表示する
        // （ダイアログのイベントループ中にプラグイン処理が走らないようにする）
        if (success) {
            QMessageBox::information(this, "エクスポート完了",
                QString("オーディオファイルをエクスポートしました:\n%1").arg(filePath));
        } else {
            QMessageBox::warning(this, "エクスポートエラー",
                "オーディオのエクスポートに失敗しました。");
        }

        // MessageBoxが閉じられた後、安全な状態でAudioEngine・タイマーを復帰
        // （ダイアログを閉じた直後の描画更新イベントなどを先に処理しておく）
        QCoreApplication::processEvents();
        m_playbackController->resumeFromExport();
    });

    Project* proj = m_project;
    watcher->setFuture(QtConcurrent::run([exporter, proj, filePath, exportSampleRate]() -> bool {
        return exporter->exportToWav(proj, filePath, exportSampleRate, 24);
    }));
}

// ===============================================================
// 電球アニメーション（放射線オーバーレイ）
// ===============================================================
void MainWindow::playBulbAnimation(bool turningOn)
{
    if (!m_raysOverlay) return;

    // 前のアニメーションを停止
    if (m_themeAnim) {
        m_themeAnim->stop();
        m_themeAnim->deleteLater();
        m_themeAnim = nullptr;
    }
    // colorize エフェクトが残っていれば除去
    if (m_themeToggleBtn) m_themeToggleBtn->setGraphicsEffect(nullptr);

    const qreal startVal = m_raysOverlay->progress();
    const qreal endVal   = turningOn ? 1.0 : 0.0;

    auto* anim = new QVariantAnimation(this);
    anim->setStartValue(startVal);
    anim->setEndValue(endVal);

    if (turningOn) {
        // 点灯: 線がすばやく放射する (100ms) → 定常状態 (progress=1.0) を維持
        anim->setDuration(320);
        anim->setEasingCurve(QEasingCurve::OutQuart);
    } else {
        // 消灯: 線がゆっくり引き戻される (progress=0.0 まで)
        anim->setDuration(280);
        anim->setEasingCurve(QEasingCurve::InQuart);
    }

    connect(anim, &QVariantAnimation::valueChanged,
            this, [this](const QVariant& v) {
                if (m_raysOverlay) m_raysOverlay->setProgress(v.toReal());
            });

    m_themeAnim = anim;
    connect(anim, &QAbstractAnimation::finished, this, [this, anim]() {
        if (m_themeAnim == anim) m_themeAnim = nullptr;
        anim->deleteLater();
        Darwin::ThemeManager::instance().toggleTheme();
    });
    anim->start();
}
