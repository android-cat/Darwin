/**
 * @file SourceView.cpp
 * @brief SourceViewクラスの実装（プラグインライブラリ / 詳細パネル / スキャン管理）
 *
 * ScanSpinnerWidget → ScanSpinnerWidget.cpp
 * PluginLoadOverlay → PluginLoadOverlay.cpp
 */
#include "SourceView.h"
#include "VST3Scanner.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QCoreApplication>
#include <algorithm>
#include <QInputDialog>
#include <QFileDialog>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <cmath>
#include "models/Project.h"
#include "models/Track.h"
#include "PluginEditorWidget.h"
#include "common/FontManager.h"
#include "common/ThemeManager.h"

// ─── SourceView ──────────────────────────────────────────

void SourceView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_scanSpinner && m_instrumentList) {
        // スピナーをリストウィジェットの中央に配置
        QPoint listCenter = m_instrumentList->geometry().center();
        m_scanSpinner->move(listCenter.x() - m_scanSpinner->width() / 2,
                            listCenter.y() - m_scanSpinner->height() / 2);
    }
}

SourceView::SourceView(QWidget *parent)
    : QWidget(parent)
    , m_instDescLabel(nullptr)
    , m_vendorLabel(nullptr)
    , m_loadBtn(nullptr)
    , m_rescanBtn(nullptr)
    , m_scanner(new VST3Scanner(this))
    , m_selectedIndex(-1)
    , m_settings(new QSettings(QSettings::IniFormat, QSettings::UserScope, "Darwin", "PluginUsage", this))
    , m_scanSpinner(nullptr)
    , m_detailFadeEffect(nullptr)
    , m_detailContentWidget(nullptr)
    , m_detailWidget(nullptr)
    , m_loadOverlay(nullptr)
{
    connect(&Darwin::ThemeManager::instance(), &Darwin::ThemeManager::themeChanged, this, &SourceView::applyTheme);

    setupUi();
    
    // スタガードリビールタイマー
    m_itemRevealTimer.setInterval(40); // アイテム間40ms
    connect(&m_itemRevealTimer, &QTimer::timeout, this, &SourceView::revealNextItem);
    
    // スキャンドットアニメーションタイマー
    connect(&m_scanDotTimer, &QTimer::timeout, this, [this](){
        m_scanDotCount = (m_scanDotCount + 1) % 4;
        QString dots = QString(".").repeated(m_scanDotCount == 0 ? 3 : m_scanDotCount);
        m_statusLabel->setText("Scanning" + dots);
    });
    
    // 起動時の自動スキャンは無効
    // rescanPlugins();
}

void SourceView::setupUi()
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ライブラリ（左側）
    QWidget *libraryWidget = new QWidget(this);
    libraryWidget->setObjectName("sourceLibrary");
    libraryWidget->setFixedWidth(260);
    // スタイルは applyTheme で適用
    
    QVBoxLayout *libLayout = new QVBoxLayout(libraryWidget);
    libLayout->setContentsMargins(16, 24, 16, 16);
    libLayout->setSpacing(12);
    
    // ヘッダー
    QHBoxLayout *headerLayout = new QHBoxLayout();
    
    QLabel *libTitle = new QLabel("LIBRARY", libraryWidget);
    libTitle->setStyleSheet("font-size: 10px; font-weight: 800; letter-spacing: 2px; color: #FF3366; background: transparent;");
    
    // パス設定ボタン
    QPushButton *settingsBtn = new QPushButton("PATHS", libraryWidget);
    settingsBtn->setObjectName("sourcePathsBtn");
    settingsBtn->setToolTip("Scan Settings");
    settingsBtn->setStyleSheet(QString(R"(
        #sourcePathsBtn {
            border: none;
            background-color: transparent;
            color: %1;
            font-size: 10px;
            font-weight: bold;
            padding: 0px 4px;
        }
        #sourcePathsBtn:hover { 
            color: #FF3366; 
        }
    )").arg(Darwin::ThemeManager::instance().secondaryTextColor().name()));
    settingsBtn->setCursor(Qt::PointingHandCursor);
    connect(settingsBtn, &QPushButton::clicked, [this](){
        // スキャンパス編集ダイアログ
        QStringList paths = m_scanner->scanPaths();
        bool ok;
        QString text = QInputDialog::getMultiLineText(this, "VST3 Scan Paths", 
            "Enter VST3 scan paths (one per line):", paths.join("\n"), &ok);
        if (ok) {
            QStringList newPaths = text.split("\n", Qt::SkipEmptyParts);
            m_scanner->setScanPaths(newPaths);
        }
    });

    // リスキャンボタン
    m_rescanBtn = new QPushButton("SCAN", libraryWidget);
    m_rescanBtn->setObjectName("sourceScanBtn");
    m_rescanBtn->setToolTip("Rescan VST3 Plugins");
    m_rescanBtn->setStyleSheet(QString(R"(
        #sourceScanBtn {
            border: none;
            background-color: transparent;
            color: %1;
            font-size: 10px;
            font-weight: bold;
            padding: 0px 4px;
        }
        #sourceScanBtn:hover { 
            color: #FF3366; 
        }
    )").arg(Darwin::ThemeManager::instance().secondaryTextColor().name()));
    m_rescanBtn->setCursor(Qt::PointingHandCursor);
    connect(m_rescanBtn, &QPushButton::clicked, this, &SourceView::onRescanClicked);
    
    // ステータスラベル
    m_statusLabel = new QLabel("Scanning...", libraryWidget);
    m_statusLabel->setStyleSheet("font-size: 10px; color: #FF3366; font-weight: bold; background: transparent;");
    m_statusLabel->hide();
    
    // スキャンスピナー
    m_scanSpinner = new ScanSpinnerWidget(libraryWidget);

    headerLayout->addWidget(libTitle);
    headerLayout->addStretch();
    headerLayout->addWidget(settingsBtn);
    headerLayout->addWidget(m_rescanBtn);
    libLayout->addLayout(headerLayout);
    
    QLabel *catTitle = new QLabel("VST3 INSTRUMENTS", libraryWidget);
    catTitle->setStyleSheet("font-size: 9px; font-weight: bold; color: #94a3b8; margin-top: 4px; background: transparent;");
    libLayout->addWidget(catTitle);
    
    // リストコンテナ（スピナーオーバーレイ用）
    QWidget* listContainer = new QWidget(libraryWidget);
    QVBoxLayout* listContainerLayout = new QVBoxLayout(listContainer);
    listContainerLayout->setContentsMargins(0,0,0,0);
    
    m_instrumentList = new QListWidget(listContainer);
    m_instrumentList->setFocusPolicy(Qt::NoFocus); // フォーカス枠線を防止
    // リストのスタイル設定は applyTheme() に委譲
    
    // スピナーをリスト上に重ねて中央配置
    m_scanSpinner = new ScanSpinnerWidget(listContainer);
    
    listContainerLayout->addWidget(m_instrumentList);
    libLayout->addWidget(listContainer, 1);
    
    connect(m_instrumentList, &QListWidget::itemClicked, this, &SourceView::onInstrumentClicked);
    
    mainLayout->addWidget(libraryWidget);

    // 詳細パネル（右側）
    QWidget *detailWidget = new QWidget(this);
    m_detailWidget = detailWidget;
    detailWidget->setObjectName("sourceDetail");
    // スタイルは applyTheme で適用
    
    QVBoxLayout *detailLayout = new QVBoxLayout(detailWidget);
    detailLayout->setContentsMargins(48, 32, 48, 32);
    
    // フェードアニメーション用コンテンツウィジェット
    m_detailContentWidget = new QWidget(detailWidget);
    m_detailFadeEffect = new QGraphicsOpacityEffect(m_detailContentWidget);
    m_detailFadeEffect->setOpacity(1.0);
    // QtのネストされたQGraphicsEffectによる位置ズレを防ぐため、アニメーション非実行時は無効化
    m_detailFadeEffect->setEnabled(false);
    m_detailContentWidget->setGraphicsEffect(m_detailFadeEffect);
    
    QVBoxLayout *contentLayout = new QVBoxLayout(m_detailContentWidget);
    contentLayout->setAlignment(Qt::AlignCenter);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    
    m_instNameLabel = new QLabel("Select an Instrument", m_detailContentWidget);
    m_instNameLabel->setStyleSheet(QString("font-size: 22px; font-weight: 300; color: %1; background: transparent;").arg(Darwin::ThemeManager::instance().textColor().name()));
    m_instNameLabel->setAlignment(Qt::AlignCenter);
    m_instNameLabel->setWordWrap(true);
    
    m_instDescLabel = new QLabel("Choose a VST3 instrument from the library to start composing.", m_detailContentWidget);
    m_instDescLabel->setStyleSheet(QString("font-size: 12px; color: %1; background: transparent;").arg(Darwin::ThemeManager::instance().secondaryTextColor().name()));
    m_instDescLabel->setAlignment(Qt::AlignCenter);
    m_instDescLabel->setWordWrap(true);
    
    m_vendorLabel = new QLabel("", m_detailContentWidget);
    m_vendorLabel->setStyleSheet("font-size: 10px; font-weight: bold; color: #FF3366; text-transform: uppercase; letter-spacing: 1px; background: transparent;");
    m_vendorLabel->setAlignment(Qt::AlignCenter);

    m_loadBtn = new QPushButton("LOAD INSTRUMENT", m_detailContentWidget);
    m_loadBtn->setObjectName("sourceLoadBtn");
    m_loadBtn->setFixedSize(200, 48);
    m_loadBtn->setCursor(Qt::PointingHandCursor);
    m_loadBtn->setEnabled(false);
    m_loadBtn->setStyleSheet(QString(R"(
        #sourceLoadBtn {
            background-color: %1;
            color: %2;
            border: none;
            border-radius: 24px;
            font-size: 12px;
            font-weight: bold;
            letter-spacing: 1px;
        }
        #sourceLoadBtn:hover {
            background-color: #FF3366;
            color: #ffffff;
        }
        #sourceLoadBtn:disabled {
            background-color: %3;
            color: %4;
        }
    )").arg(Darwin::ThemeManager::instance().isDarkMode() ? "#e2e8f0" : "#333333",
            Darwin::ThemeManager::instance().isDarkMode() ? "#0f172a" : "#ffffff",
            Darwin::ThemeManager::instance().gridLineColor().name(),
            Darwin::ThemeManager::instance().secondaryTextColor().name()));
    
    connect(m_loadBtn, &QPushButton::clicked, this, &SourceView::onLoadButtonClicked);

    contentLayout->addStretch();
    contentLayout->addWidget(m_instNameLabel);
    contentLayout->addSpacing(8);
    contentLayout->addWidget(m_vendorLabel);
    contentLayout->addSpacing(12);
    contentLayout->addWidget(m_instDescLabel);
    contentLayout->addSpacing(32);
    
    // LOADボタンを中央揃え
    QHBoxLayout *loadBtnLayout = new QHBoxLayout();
    loadBtnLayout->addStretch();
    loadBtnLayout->addWidget(m_loadBtn);
    loadBtnLayout->addStretch();
    contentLayout->addLayout(loadBtnLayout);
    
    contentLayout->addStretch();
    
    detailLayout->addWidget(m_detailContentWidget);

    // プラグインロードオーバーレイ（詳細パネルの子）
    m_loadOverlay = new PluginLoadOverlay(detailWidget);
    connect(m_loadOverlay, &PluginLoadOverlay::finished, this, [this](){
        m_isLoading = false;
        m_loadBtn->setText("LOAD INSTRUMENT");
        m_loadBtn->setEnabled(true);
        m_loadBtn->setStyleSheet(R"(
            #sourceLoadBtn {
                background-color: #333;
                color: white;
                border: none;
                border-radius: 24px;
                font-size: 12px;
                font-weight: bold;
                letter-spacing: 1px;
            }
            #sourceLoadBtn:hover {
                background-color: #FF3366;
            }
            #sourceLoadBtn:disabled {
                background-color: #e2e8f0;
                color: #94a3b8;
            }
        )");
        if (m_selectedIndex >= 0 && m_selectedIndex < m_plugins.size())
            updateDetailCard(m_plugins[m_selectedIndex]);
    });
    
    mainLayout->addWidget(detailWidget);
    mainLayout->setStretch(0, 0); // Library: 固定幅
    mainLayout->setStretch(1, 1); // Detail: 残り全部

    applyTheme(); // 初期テーマ反映
}

void SourceView::applyTheme()
{
    const Darwin::ThemeManager& tm = Darwin::ThemeManager::instance();

    QWidget* libraryWidget = findChild<QWidget*>("sourceLibrary");
    if (libraryWidget) {
        libraryWidget->setStyleSheet(QString("#sourceLibrary { background-color: %1; }").arg(tm.backgroundColor().name()));
    }

    if (m_detailWidget) {
        m_detailWidget->setStyleSheet(QString("#sourceDetail { background-color: %1; border-left: 1px solid %2; }")
            .arg(tm.backgroundColor().name(), tm.borderColor().name()));
    }

    if (m_instNameLabel) {
        m_instNameLabel->setStyleSheet(QString("font-size: 22px; font-weight: 300; color: %1; background: transparent;").arg(tm.textColor().name()));
    }
    
    if (m_instDescLabel) {
        m_instDescLabel->setStyleSheet(QString("font-size: 12px; color: %1; background: transparent;").arg(tm.secondaryTextColor().name()));
    }
    
    if (m_loadBtn) {
        m_loadBtn->setStyleSheet(QString(R"(
            #sourceLoadBtn {
                background-color: %1;
                color: %2;
                border: none;
                border-radius: 24px;
                font-size: 12px;
                font-weight: bold;
                letter-spacing: 1px;
            }
            #sourceLoadBtn:hover {
                background-color: #FF3366;
                color: #ffffff;
            }
            #sourceLoadBtn:disabled {
                background-color: %3;
                color: %4;
            }
        )").arg(tm.isDarkMode() ? "#e2e8f0" : "#333333",
                tm.isDarkMode() ? "#0f172a" : "#ffffff",
                tm.gridLineColor().name(),
                tm.secondaryTextColor().name()));
    }

    if (m_instrumentList) {
        m_instrumentList->setStyleSheet(QString(R"(
            QListWidget {
                border: none;
                background-color: transparent;
                font-family: %5;
                font-size: 12px;
                outline: none;
            }
            QListWidget::item {
                padding: 10px 12px;
                border: 1px solid %3;
                border-radius: 4px;
                margin-bottom: 2px;
                color: %1;
                outline: none;
            }
            QListWidget::item:selected {
                background-color: %4;
                border: 1px solid #FF3366;
                color: %1;
            }
            QListWidget::item:hover:!selected {
                border-color: %2;
            }
            QListWidget::item:focus {
                outline: none;
                border: 1px solid #FF3366;
            }
        )").arg(tm.textColor().name(),
                tm.gridLineSubBeatColor().name(),
                tm.backgroundColor().name(),
                tm.isDarkMode() ? "#3f1a26" : "#fff0f3",
                Darwin::FontManager::uiFontCss()));
        
        // 強制的にリストアイテムのテキスト色も更新する
        for (int i = 0; i < m_instrumentList->count(); ++i) {
            QListWidgetItem* item = m_instrumentList->item(i);
            if (item) {
                // リビール中の場合は透明になっている可能性があるため、完全な透明でない場合のみ更新
                if (item->foreground().color().alpha() > 0) {
                    item->setForeground(tm.textColor());
                }
            }
        }
    }
}

void SourceView::rescanPlugins()
{
    // スキャンアニメーション開始
    startScanAnimation();
    
    m_loadBtn->setEnabled(false);
    m_selectedIndex = -1;
    m_plugins.clear();

    // QtConcurrentで並列スキャン
    QFuture<QVector<VST3PluginInfo>> future = QtConcurrent::run([this]() {
        return m_scanner->scan(true);
    });

    // メインスレッドで結果を処理するウォッチャー
    QFutureWatcher<QVector<VST3PluginInfo>>* watcher = new QFutureWatcher<QVector<VST3PluginInfo>>(this);
    connect(watcher, &QFutureWatcher<QVector<VST3PluginInfo>>::finished, this, [this, watcher]() {
        QVector<VST3PluginInfo> plugins = watcher->result();
        
        stopScanAnimation();
        updatePluginList(plugins);
        startStaggeredReveal();
        
        watcher->deleteLater();
    });

    watcher->setFuture(future);
}

void SourceView::updatePluginList(const QVector<VST3PluginInfo>& plugins)
{
    m_plugins = plugins;
    m_instrumentList->clear();
    m_selectedIndex = -1;

    // 使用データをロードしてソート
    loadUsageData();
    sortPluginsByUsage();
    
    populateInstrumentList();
    
    if (m_plugins.isEmpty()) {
        m_instNameLabel->setText("No Instruments Found");
        m_instDescLabel->setText("No VST3 instrument plugins found.\nCheck Settings to add VST3 paths.");
        m_vendorLabel->setText("");
    } else {
        m_instNameLabel->setText("Select an Instrument");
        m_instDescLabel->setText(QString("Found %1 VST3 instrument(s).").arg(m_plugins.size()));
        m_vendorLabel->setText("");
    }
}

void SourceView::populateInstrumentList()
{
    for (const auto& plugin : m_plugins) {
        QListWidgetItem* item = new QListWidgetItem(plugin.name);
        item->setToolTip(plugin.path);
        m_instrumentList->addItem(item);
    }
}

void SourceView::onInstrumentClicked(QListWidgetItem* item)
{
    if (!item) return;
    
    int row = m_instrumentList->row(item);
    if (row >= 0 && row < m_plugins.size()) {
        m_selectedIndex = row;
        const VST3PluginInfo& info = m_plugins[row];
        
        // 詳細カードのトランジションアニメーション
        animateDetailCard(info);
        m_loadBtn->setEnabled(true);
        
        emit instrumentSelected(info.name, info.path);
    }
}

void SourceView::onLoadButtonClicked()
{
    if (m_isLoading) return; // ダブルクリック防止
    
    if (m_selectedIndex >= 0 && m_selectedIndex < m_plugins.size()) {
        const VST3PluginInfo info = m_plugins[m_selectedIndex];
        m_isLoading = true;
        
        // 使用回数をインクリメント
        incrementUsageCount(info.path);
        
        // 詳細パネル内にオーバーレイを表示
        if (m_loadOverlay && m_detailWidget) {
            m_loadOverlay->setGeometry(m_detailWidget->rect());
            m_loadOverlay->startLoading(info.name);
        }
        
        m_loadBtn->setEnabled(false);
        m_loadBtn->setText("LOADING...");
        
        // オーバーレイアニメーション開始後に同期ロードをemit
        QString name = info.name;
        QString path = info.path;
        QTimer::singleShot(200, this, [this, name, path](){
            emit loadInstrumentRequested(name, path);
            // MainWindowがロード後にonPluginLoaded()を呼ぶ
        });
    }
}

void SourceView::onPluginLoaded(bool success, const QString& error)
{
    if (m_loadOverlay) {
        if (success)
            m_loadOverlay->showSuccess();
        else
            m_loadOverlay->showFailure(error);
    }
}

void SourceView::onRescanClicked()
{
    rescanPlugins();
}

void SourceView::updateDetailCard(const VST3PluginInfo& info)
{
    if (m_instNameLabel) {
        m_instNameLabel->setText(info.name.toUpper());
    }
    if (m_vendorLabel) {
        m_vendorLabel->setText(info.vendor.isEmpty() ? "Unknown Vendor" : info.vendor);
    }
    if (m_instDescLabel) {
        QString ver = info.version.isEmpty() ? "-" : info.version;
        QString cat = info.category.isEmpty() ? "Instrument" : info.category;
        
        QString desc = QString("Version: %1\nCategory: %2\nUsed: %3 times")
            .arg(ver)
            .arg(cat)
            .arg(info.usageCount);
        m_instDescLabel->setText(desc);
    }
}

void SourceView::sortPluginsByUsage()
{
    // 使用回数降順、同数なら名前昇順
    std::sort(m_plugins.begin(), m_plugins.end(), 
        [](const VST3PluginInfo& a, const VST3PluginInfo& b) {
            if (a.usageCount != b.usageCount) {
                return a.usageCount > b.usageCount;
            }
            return a.name.toLower() < b.name.toLower();
        }
    );
}

void SourceView::loadUsageData()
{
    m_settings->beginGroup("UsageCount");
    for (auto& plugin : m_plugins) {
        QString key = QString(plugin.path.toUtf8().toBase64());
        plugin.usageCount = m_settings->value(key, 0).toInt();
    }
    m_settings->endGroup();
}

void SourceView::saveUsageData()
{
    m_settings->beginGroup("UsageCount");
    for (const auto& plugin : m_plugins) {
        QString key = QString(plugin.path.toUtf8().toBase64());
        m_settings->setValue(key, plugin.usageCount);
    }
    m_settings->endGroup();
    m_settings->sync();
}

void SourceView::incrementUsageCount(const QString& pluginPath)
{
    for (auto& plugin : m_plugins) {
        if (plugin.path == pluginPath) {
            plugin.usageCount++;
            break;
        }
    }
    saveUsageData();
    
    // 再ソートしてリストを更新
    sortPluginsByUsage();
    m_instrumentList->clear();
    populateInstrumentList();
    
    // ロードされたプラグインを再選択
    for (int i = 0; i < m_plugins.size(); ++i) {
        if (m_plugins[i].path == pluginPath) {
            m_instrumentList->setCurrentRow(i);
            m_selectedIndex = i;
            break;
        }
    }
}

// ─── アニメーション実装 ─────────────────────────────────

void SourceView::startScanAnimation()
{
    // スキャン中UIに切り替え
    m_instrumentList->clear();
    m_rescanBtn->setEnabled(false);
    
    // スキャンボタンの色をパルス
    m_rescanBtn->setStyleSheet(R"(
        #sourceScanBtn {
            border: none;
            background-color: transparent;
            color: #FF3366;
            font-size: 10px;
            font-weight: bold;
            padding: 0px 4px;
        }
    )");
    
    // スピナー表示
    m_scanSpinner->start();
    m_scanDotCount = 0;
    m_scanDotTimer.start(400);
    
    // 詳細カードをクリア
    if (m_instNameLabel) m_instNameLabel->setText("");
    if (m_vendorLabel) m_vendorLabel->setText("");
    if (m_instDescLabel) m_instDescLabel->setText("");
}

void SourceView::stopScanAnimation()
{
    m_scanSpinner->stop();
    m_scanDotTimer.stop();
    m_statusLabel->hide();
    m_rescanBtn->setEnabled(true);
    
    // スキャンボタンスタイルを復元
    m_rescanBtn->setStyleSheet(R"(
        #sourceScanBtn {
            border: none;
            background-color: transparent;
            color: #94a3b8;
            font-size: 10px;
            font-weight: bold;
            padding: 0px 4px;
        }
        #sourceScanBtn:hover { 
            color: #FF3366; 
        }
    )");
}

void SourceView::startStaggeredReveal()
{
    // 全アイテムを最初は透明に
    for (int i = 0; i < m_instrumentList->count(); ++i) {
        QListWidgetItem* item = m_instrumentList->item(i);
        if (item) {
            item->setForeground(QColor(0, 0, 0, 0));
            item->setBackground(QColor(0, 0, 0, 0));
        }
    }
    
    m_itemRevealIndex = 0;
    m_itemRevealTimer.start();
}

void SourceView::revealNextItem()
{
    if (m_itemRevealIndex >= m_instrumentList->count()) {
        m_itemRevealTimer.stop();
        return;
    }
    
    QListWidgetItem* item = m_instrumentList->item(m_itemRevealIndex);
    if (item) {
        QWidget* itemWidget = m_instrumentList->itemWidget(item);
        Q_UNUSED(itemWidget);
        
        // テキスト色を復元してリビール
        item->setForeground(Darwin::ThemeManager::instance().textColor());
        
        // フェードイン用の一時QLabelオーバーレイ
        QRect itemRect = m_instrumentList->visualItemRect(item);
        if (!itemRect.isNull()) {
            QLabel* fadeLabel = new QLabel(item->text(), m_instrumentList->viewport());
            fadeLabel->setGeometry(itemRect);
            fadeLabel->setStyleSheet(
                QString("padding: 10px 12px; border: 1px solid %3; border-radius: 4px; "
                "color: %1; background: %2; font-family: %4; font-size: 12px;")
                .arg(Darwin::ThemeManager::instance().textColor().name(),
                     Darwin::ThemeManager::instance().backgroundColor().name(),
                     Darwin::ThemeManager::instance().backgroundColor().name(),
                     Darwin::FontManager::uiFontCss())
            );
            fadeLabel->show();
            
            QGraphicsOpacityEffect* effect = new QGraphicsOpacityEffect(fadeLabel);
            fadeLabel->setGraphicsEffect(effect);
            
            QPropertyAnimation* anim = new QPropertyAnimation(effect, "opacity", fadeLabel);
            anim->setDuration(300);
            anim->setStartValue(0.0);
            anim->setEndValue(1.0);
            anim->setEasingCurve(QEasingCurve::OutCubic);
            
            connect(anim, &QPropertyAnimation::finished, fadeLabel, &QLabel::deleteLater);
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        }
    }
    
    m_itemRevealIndex++;
}

void SourceView::animateDetailCard(const VST3PluginInfo& info)
{
    if (!m_detailFadeEffect || !m_detailContentWidget) {
        updateDetailCard(info);
        return;
    }
    
    // Qt GraphicsEffectのネスト不具合を回避するため実行時だけ有効化
    m_detailFadeEffect->setEnabled(true);

    // フェードアウト → 更新 → フェードイン
    QPropertyAnimation* fadeOut = new QPropertyAnimation(m_detailFadeEffect, "opacity", this);
    fadeOut->setDuration(120);
    fadeOut->setStartValue(1.0);
    fadeOut->setEndValue(0.0);
    fadeOut->setEasingCurve(QEasingCurve::OutCubic);
    
    connect(fadeOut, &QPropertyAnimation::finished, this, [this, info](){
        updateDetailCard(info);
        
        QPropertyAnimation* fadeIn = new QPropertyAnimation(m_detailFadeEffect, "opacity", this);
        fadeIn->setDuration(250);
        fadeIn->setStartValue(0.0);
        fadeIn->setEndValue(1.0);
        fadeIn->setEasingCurve(QEasingCurve::OutCubic);
        connect(fadeIn, &QPropertyAnimation::finished, this, [this](){
            if (m_detailFadeEffect) m_detailFadeEffect->setEnabled(false);
        });
        fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
    });
    
    fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
}

