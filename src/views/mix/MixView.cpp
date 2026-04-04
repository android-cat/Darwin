#include "MixView.h"
#include "MixerChannelWidget.h"
#include "Project.h"
#include "Track.h"
#include "PlaybackController.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QFileInfo>
#include <QFrame>
#include <QSplitter>
#include "../plugineditor/PluginEditorWidget.h"
#include "../../plugins/VST3PluginInstance.h"
#include "common/FontManager.h"
#include "common/ThemeManager.h"

// レイアウト定数
namespace {
    constexpr int MIXER_MARGIN = 32;
    constexpr int MIXER_SPACING = 24;
    constexpr int INSPECTOR_WIDTH = 384;
    constexpr int FOLDER_EXPANDED_MARGIN = 12;
    constexpr int FOLDER_EXPANDED_SPACING = 16;
    constexpr int FOLDER_COLLAPSED_MARGIN_H = 6;
    constexpr int FOLDER_COLLAPSED_MARGIN_V = 12;
    constexpr int FOLDER_COLLAPSED_SPACING = 8;
    constexpr int FOLDER_BADGE_SIZE = 24;
    constexpr int FOLDER_BADGE_RADIUS = 12;
    constexpr int FOLDER_FRAME_RADIUS = 12;
    // マスターチャンネルの識別用番号（通常トラック番号は1始まりなので衝突しない）
    constexpr int MASTER_CHANNEL_NUMBER = -1;
}

MixView::MixView(QWidget *parent) 
    : QWidget(parent)
    , m_project(nullptr)
    , m_playbackController(nullptr)
    , m_mixerLayout(nullptr)
    , m_inspectorLayout(nullptr)
    , m_currentPluginEditor(nullptr)
    , m_currentPlugin(nullptr)
{    // Theme setup
    connect(&Darwin::ThemeManager::instance(), &Darwin::ThemeManager::themeChanged, this, &MixView::applyTheme);
    setupUi();
}

void MixView::setupUi()
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // メインコンテナ（ミキサー | インスペクター）を QSplitter に変更してリサイズ可能にする
    QSplitter *centralSplitter = new QSplitter(Qt::Horizontal, this);
    centralSplitter->setHandleWidth(2);
    // スプリッターのハンドルプロパティ設定（背景に溶け込むように）
    centralSplitter->setStyleSheet(QString(
        "QSplitter::handle { background-color: %1; }"
    ).arg(Darwin::ThemeManager::instance().borderColor().name()));
    layout->addWidget(centralSplitter);

    // ミキサースクロール領域（左側）
    QScrollArea *mixerScroll = new QScrollArea(centralSplitter);
    mixerScroll->setWidgetResizable(true);
    mixerScroll->setFrameShape(QFrame::NoFrame);
    mixerScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mixerScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QWidget *mixerContent = new QWidget(mixerScroll);
    // スタイルは applyTheme() で設定
    m_mixerLayout = new QHBoxLayout(mixerContent);
    m_mixerLayout->setContentsMargins(MIXER_MARGIN, MIXER_MARGIN, MIXER_MARGIN, MIXER_MARGIN);
    m_mixerLayout->setSpacing(MIXER_SPACING);
    m_mixerLayout->setAlignment(Qt::AlignLeft);

    mixerScroll->setWidget(mixerContent);
    centralSplitter->addWidget(mixerScroll);

    // インスペクター（右側）
    QWidget *inspectorContainer = new QWidget(centralSplitter);
    inspectorContainer->setMinimumWidth(150); // スプリッターでリサイズ・縮小可能にするための最小幅
    // スタイルは applyTheme() で設定

    m_inspectorLayout = new QVBoxLayout(inspectorContainer);
    m_inspectorLayout->setContentsMargins(0, 0, 0, 0);

    centralSplitter->addWidget(inspectorContainer);
    
    // スプリッターの初期比率を設定 (例: 左 1 : 右 0 → インスペクターはINSPECTOR_WIDTHに初期設定)
    centralSplitter->setSizes({1000, INSPECTOR_WIDTH});

    applyTheme(); // 初期テーマの適用
}

void MixView::applyTheme()
{
    // 背景色
    this->setStyleSheet(QString("background-color: %1;").arg(Darwin::ThemeManager::instance().backgroundColor().name()));

    // ミキサーコンテンツエリア
    QWidget* mixerContent = m_mixerLayout->parentWidget();
    if (mixerContent) {
        mixerContent->setStyleSheet(QString("background-color: %1;").arg(Darwin::ThemeManager::instance().backgroundColor().name()));
    }

    // インスペクター領域
    QWidget* inspectorContainer = m_inspectorLayout->parentWidget();
    if (inspectorContainer) {
        inspectorContainer->setStyleSheet(QString(R"(
            background-color: %1;
            border-left: 1px solid %2;
        )").arg(Darwin::ThemeManager::instance().backgroundColor().name(),
                Darwin::ThemeManager::instance().borderColor().name()));
    }

    // フォルダフレームのボーダーカラーを更新
    const QString borderCol = Darwin::ThemeManager::instance().borderColor().name();
    const QString frameStyle = QString(R"(
        #folderFrame {
            background-color: transparent;
            border: 1px solid %1;
            border-radius: 6px;
        }
    )").arg(borderCol);
    for (QFrame* frame : findChildren<QFrame*>("folderFrame")) {
        frame->setStyleSheet(frameStyle);
    }
}

void MixView::setProject(Project* project)
{
    if (m_project) {
        // 旧プロジェクトからの全シグナルを切断（ラムダ接続含む）
        disconnect(m_project, nullptr, this, nullptr);
    }

    m_project = project;

    if (m_project) {
        connect(m_project, &Project::trackAdded, this, &MixView::onTrackAdded);
        connect(m_project, &Project::trackRemoved, this, &MixView::onTrackRemoved);
        connect(m_project, &Project::trackOrderChanged, this, [this]() { buildMixerChannels(); });
        connect(m_project, &Project::folderStructureChanged, this, [this]() { buildMixerChannels(); });
    }

    buildMixerChannels();
}

void MixView::setPlaybackController(PlaybackController* playbackController)
{
    // 旧コントローラーからのシグナルを切断（ラムダ接続含む）
    if (m_playbackController) {
        disconnect(m_playbackController, nullptr, this, nullptr);
    }

    m_playbackController = playbackController;
    buildMixerChannels();
}

void MixView::onTrackAdded(Track* track)
{
    Q_UNUSED(track);
    buildMixerChannels();
}

void MixView::onTrackRemoved(Track* track)
{
    Q_UNUSED(track);
    buildMixerChannels();
}

void MixView::buildMixerChannels()
{
    if (!m_mixerLayout) return;

    // 既存チャンネルをクリア
    clearLayout(m_mixerLayout);
    m_trackWidgetMap.clear();
    m_folderWidgets.clear();

    if (m_project) {
        // ルートレベルのトラック/フォルダのみを処理（parentFolderId < 0）
        // ネストされたフォルダ/トラックは buildFolderGroup() で再帰的に処理
        int trackNumber = 1;
        for (Track* track : m_project->tracks()) {
            // フォルダに属するトラックはスキップ（親フォルダの展開時に処理される）
            if (track->parentFolderId() >= 0) continue;

            if (track->isFolder()) {
                buildFolderGroup(track, m_mixerLayout, trackNumber);
            } else {
                MixerChannelWidget* mcw = createChannelWidget(trackNumber++, track);
                m_mixerLayout->addWidget(mcw);
                m_trackWidgetMap[track->id()] = mcw;
            }
        }
    }

    // マスターチャンネルとの間にスペース
    m_mixerLayout->addSpacing(MIXER_SPACING);

    // マスターチャンネル
    Track* masterTrack = m_project ? m_project->masterTrack() : nullptr;
    MixerChannelWidget *master = new MixerChannelWidget(
        MASTER_CHANNEL_NUMBER, "Master", masterTrack);
    connect(master, &MixerChannelWidget::pluginEditorRequested,
            this, &MixView::onPluginEditorRequested);

    // 注: propertyChanged → updateFxSlots は MixerChannelWidget コンストラクタで
    //     既に接続済み。ここで再接続すると二重呼び出しになるため省略。

    connectPlaybackMetering(master);
    m_mixerLayout->addWidget(master);

    // 追加: 余った領域を埋めるストレッチを入れることで、各チャンネルウィジェットが不必要に広がらないようにする
    m_mixerLayout->addStretch();
}

/**
 * @brief フォルダグループを再帰的に構築する
 *
 * フォルダチャンネル + 子トラックをフォルダカラーの薄い角丸コンテナで囲む。
 * ネストされたフォルダも再帰的に処理される。
 */
void MixView::buildFolderGroup(Track* folder, QHBoxLayout* parentLayout, int& trackNumber)
{
    if (!folder || !folder->isFolder() || !m_project) return;

    const QList<Track*> children = m_project->folderChildren(folder);
    const bool expanded = folder->isFolderExpanded();

    // フォルダ全体を囲むコンテナ（薄いボーダーの角丸矩形）
    QFrame* folderFrame = new QFrame(parentLayout->parentWidget());
    folderFrame->setObjectName("folderFrame");
    folderFrame->setStyleSheet(QString(R"(
        #folderFrame {
            background-color: transparent;
            border: 1px solid %2;
            border-radius: %1px;
        }
    )").arg(FOLDER_FRAME_RADIUS).arg(Darwin::ThemeManager::instance().borderColor().name()));
    // 横は中身に合わせて縮む、縦は親に合わせて伸びる
    folderFrame->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    QHBoxLayout* folderLayout = new QHBoxLayout(folderFrame);
    if (expanded) {
        folderLayout->setContentsMargins(
            FOLDER_EXPANDED_MARGIN, FOLDER_EXPANDED_MARGIN,
            FOLDER_EXPANDED_MARGIN, FOLDER_EXPANDED_MARGIN);
        folderLayout->setSpacing(FOLDER_EXPANDED_SPACING);
    } else {
        // 折りたたみ時は横幅をコンパクトに
        folderLayout->setContentsMargins(
            FOLDER_COLLAPSED_MARGIN_H, FOLDER_COLLAPSED_MARGIN_V,
            FOLDER_COLLAPSED_MARGIN_H, FOLDER_COLLAPSED_MARGIN_V);
        folderLayout->setSpacing(FOLDER_COLLAPSED_SPACING);
    }
    folderLayout->setAlignment(Qt::AlignLeft);

    // フォルダチャンネル（フェーダー・パン・FX付き）
    MixerChannelWidget* folderMcw = createChannelWidget(trackNumber++, folder);
    // フォルダ展開/折りたたみトグル
    connect(folderMcw, &MixerChannelWidget::folderToggleRequested,
            this, [this](Track* folderTrack) {
        if (folderTrack) {
            folderTrack->setFolderExpanded(!folderTrack->isFolderExpanded());
            buildMixerChannels();
        }
    });
    folderLayout->addWidget(folderMcw);
    m_folderWidgets[folder->id()] = folderMcw;

    // 子トラック（フォルダが展開されている場合のみ表示）
    if (expanded) {
        for (Track* childTrack : children) {
            if (childTrack->isFolder()) {
                buildFolderGroup(childTrack, folderLayout, trackNumber);
            } else {
                MixerChannelWidget* mcw = createChannelWidget(trackNumber++, childTrack);
                folderLayout->addWidget(mcw);
                m_trackWidgetMap[childTrack->id()] = mcw;
            }
        }
    } else if (!children.isEmpty()) {
        // 折りたたみ時: 子トラック数のバッジ
        QLabel* countLabel = new QLabel(QString::number(children.size()), folderFrame);
        countLabel->setFixedSize(FOLDER_BADGE_SIZE, FOLDER_BADGE_SIZE);
        countLabel->setAlignment(Qt::AlignCenter);
        countLabel->setStyleSheet(QString(R"(
            background-color: %1;
            color: white;
            border-radius: %2px;
            border: none;
            font-family: %3;
            font-size: 9px;
            font-weight: 700;
        )").arg(folder->color().name())
            .arg(FOLDER_BADGE_RADIUS)
            .arg(Darwin::FontManager::uiFontCss()));
        folderLayout->addWidget(countLabel, 0, Qt::AlignVCenter);
    }

    parentLayout->addWidget(folderFrame);
}

void MixView::onPluginEditorRequested(VST3PluginInstance* plugin)
{
    // nullptr が渡された場合、現在のエディタを閉じる
    if (!plugin) {
        closeCurrentPluginEditor();
        return;
    }

    // 同じプラグインが既に開かれている場合は何もしない
    if (m_currentPluginEditor && m_currentPlugin == plugin) {
        return;
    }

    // 既存のエディタがあれば閉じる
    closeCurrentPluginEditor();

    // ストレッチの前（インデックス0）に挿入
    // レイアウト構成: [PluginEditor] [Stretch]
    constexpr int INSERT_INDEX = 0;

    m_currentPluginEditor = new PluginEditorWidget(this);

    if (m_currentPluginEditor->openEditor(plugin)) {
        // stretch=1 でレイアウト全体に広がるようにする
        m_inspectorLayout->insertWidget(INSERT_INDEX, m_currentPluginEditor, 1);
        m_currentPluginEditor->show();
        m_currentPlugin = plugin;
    } else {
        // エディタの起動に失敗
        delete m_currentPluginEditor;
        m_currentPluginEditor = nullptr;
    }
}

MixerChannelWidget* MixView::createChannelWidget(int trackNumber, Track* track)
{
    MixerChannelWidget* mcw = new MixerChannelWidget(trackNumber, track->name(), track);
    connect(mcw, &MixerChannelWidget::pluginEditorRequested,
            this, &MixView::onPluginEditorRequested);
    return mcw;
}

void MixView::clearLayout(QLayout* layout)
{
    QLayoutItem* child;
    while ((child = layout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }
}

void MixView::closeCurrentPluginEditor()
{
    if (!m_currentPluginEditor) return;

    m_currentPluginEditor->closeEditor();
    m_inspectorLayout->removeWidget(m_currentPluginEditor);
    m_currentPluginEditor->deleteLater();
    m_currentPluginEditor = nullptr;
    m_currentPlugin = nullptr;
}

void MixView::connectPlaybackMetering(MixerChannelWidget* master)
{
    if (!m_playbackController) return;

    // 前回の buildMixerChannels() で接続されたラムダを切断してから再接続
    // （masterLevelChanged の旧接続先は deleteLater 済みウィジェットなので自動切断されるが、
    //   trackLevelChanged → this のラムダは蓄積するため明示的に切断が必要）
    disconnect(m_playbackController, nullptr, this, nullptr);

    connect(m_playbackController, &PlaybackController::masterLevelChanged,
            master, &MixerChannelWidget::setLevel);

    // trackIndex はプロジェクトの tracks() リストのインデックス → trackId に変換してマップから検索
    // フォルダトラックのレベルはオーディオエンジンが直接計算・送信する
    connect(m_playbackController, &PlaybackController::trackLevelChanged,
            this, [this](int trackIndex, float left, float right) {
        if (!m_project) return;
        const auto& tracks = m_project->tracks();
        if (trackIndex < 0 || trackIndex >= tracks.size()) return;

        const int trackId = tracks[trackIndex]->id();

        // 通常トラックのメーター更新
        if (auto it = m_trackWidgetMap.find(trackId); it != m_trackWidgetMap.end()) {
            it.value()->setLevel(left, right);
        }

        // フォルダトラックのメーター更新
        if (auto fit = m_folderWidgets.find(trackId); fit != m_folderWidgets.end()) {
            fit.value()->setLevel(left, right);
        }
    });
}
