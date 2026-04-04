#include "MixerChannelWidget.h"
#include "KnobWidget.h"
#include "FaderWidget.h"
#include "LevelMeterWidget.h"
#include "CustomTooltip.h"
#include "IconButton.h"
#include "Track.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QPushButton>
#include <QMenu>
#include <QAction>
#include <QCursor>
#include <QProgressDialog>
#include <QApplication>
#include <QMouseEvent>
#include <QDateTime>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <memory>
#include <QTimer>
#include <QFontMetrics>
#include <QDrag>
#include <QMimeData>
#include <QUrl>
#include <QSettings>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include "../plugins/VST3Scanner.h"
#include "../plugins/VST3PluginInstance.h"
#include "common/FontManager.h"
#include "common/ThemeManager.h"
#include <QDebug>

namespace {
    static QVector<VST3PluginInfo> s_cachedPlugins;
    static bool s_hasScanned = false;
}

MixerChannelWidget::MixerChannelWidget(int trackNumber, const QString &trackName, Track* track, QWidget *parent)
    : QWidget(parent)
    , m_track(track)
    , m_levelMeterL(nullptr)
    , m_levelMeterR(nullptr)
    , m_fxContainer(nullptr)
    , m_fxLayout(nullptr)
    , m_scanner(nullptr)
{
    connect(&Darwin::ThemeManager::instance(), &Darwin::ThemeManager::themeChanged, this, &MixerChannelWidget::applyTheme);

    setAcceptDrops(true);

    setFixedWidth(120); // Matched closely to image
    
    // マスターチャンネル判定（番号が負またはトラック名が "Master"）
    bool isMaster = (trackNumber < 0) || (trackName.compare("Master", Qt::CaseInsensitive) == 0);
    bool isFolder = m_track && m_track->isFolder();
    
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 0, 8, 16); // px-2 pb-4
    layout->setSpacing(8);

    // フォルダトラック: 上部にフォルダカラーのアクセントバーを表示
    if (isFolder) {
        QWidget* folderAccent = new QWidget(this);
        folderAccent->setFixedHeight(4);
        folderAccent->setStyleSheet(QString(
            "background-color: %1; border-radius: 2px;"
        ).arg(m_track->color().name()));
        layout->addWidget(folderAccent);
    }

    // Fader Section (Top)
    QFrame* faderSection = new QFrame(this);
    faderSection->setObjectName("faderSection");
    faderSection->setMinimumHeight(140);
    faderSection->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);

    if (isFolder) {
        // 背景色(パネル背景)とトラックカラーを混合し、透明な薄い色を表現する
        QColor baseColor = m_track->color();
        QColor bg = Darwin::ThemeManager::instance().panelBackgroundColor();
        QColor lightCol(
            (baseColor.red() * 15 + bg.red() * 85) / 100,
            (baseColor.green() * 15 + bg.green() * 85) / 100,
            (baseColor.blue() * 15 + bg.blue() * 85) / 100
        );
        QColor lightBorder(
            (baseColor.red() * 30 + bg.red() * 70) / 100,
            (baseColor.green() * 30 + bg.green() * 70) / 100,
            (baseColor.blue() * 30 + bg.blue() * 70) / 100
        );
        faderSection->setStyleSheet(QString(
            "#faderSection {"
            "  background-color: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 12px;"
            "}"
        ).arg(lightCol.name(), lightBorder.name()));
    } else {
        faderSection->setStyleSheet(QString(
            "#faderSection {"
            "  background-color: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 12px;"
            "}"
        ).arg(Darwin::ThemeManager::instance().panelBackgroundColor().name(),
              Darwin::ThemeManager::instance().borderColor().name()));
    }

    QHBoxLayout* faderLayout = new QHBoxLayout(faderSection);
    faderLayout->setContentsMargins(12, 16, 12, 16); // padding around fader and meter
    faderLayout->setSpacing(6);

    m_levelMeterL = new LevelMeterWidget(faderSection);
    faderLayout->addWidget(m_levelMeterL);

    FaderWidget* fader = new FaderWidget(faderSection);
    if (m_track) {
        // linear gain → dB → スライダー位置 に変換して初期値を設定
        float vol = static_cast<float>(m_track->volume());
        float initDb = FaderWidget::linearToDb(vol);
        float initPos = FaderWidget::dbToPosition(initDb);
        fader->setValue(initPos);
        // スライダー位置(0〜1) → dB → linear gain → Track::setVolume
        connect(fader, &FaderWidget::valueChanged, m_track, [this](float pos) {
            float db     = FaderWidget::positionToDb(pos);
            float linear = FaderWidget::dbToLinear(db);
            m_track->setVolume(static_cast<double>(linear));
        });
    } else if (isMaster) {
        // マスターはデフォルト 0dB（linear = 1.0）
        fader->setValue(FaderWidget::dbToPosition(0.0f));
    }
    faderLayout->addWidget(fader);

    m_levelMeterR = new LevelMeterWidget(faderSection);
    faderLayout->addWidget(m_levelMeterR);

    // 縦方向の可変領域はフェーダー部だけに寄せ、下の固定UIを押し潰さない。
    layout->addWidget(faderSection, 1);

    // Value Label for Fader（dB表示）
    auto makeDbText = [](float pos) -> QString {
        float db = FaderWidget::positionToDb(pos);
        if (std::isinf(db)) return QString("-\u221e dB"); // -∞ dB
        return QString("%1%2 dB")
            .arg(db >= 0.0f ? "+" : "")
            .arg(db, 0, 'f', 1);
    };
    QLabel* volLabel = new QLabel(makeDbText(fader->value()), this);
    volLabel->setObjectName("volLabel");
    volLabel->setAlignment(Qt::AlignCenter);
    volLabel->setStyleSheet(QString("font-family: %2; font-size: 9px; color: %1;")
        .arg(Darwin::ThemeManager::instance().secondaryTextColor().name(),
             Darwin::FontManager::uiFontCss()));
    layout->addWidget(volLabel);

    connect(fader, &FaderWidget::valueChanged, [volLabel, makeDbText](float val) {
        volLabel->setText(makeDbText(val));
    });

    // FX Slots Container
    m_fxContainer = new QWidget(this);
    m_fxLayout = new QVBoxLayout(m_fxContainer);
    m_fxLayout->setContentsMargins(0, 0, 0, 0);
    // spacing を 0 にして、ボタン間の隙間は固定高さのスペーサーアイテムで管理する。
    // layout spacing に頼ると activate() のタイミングで Qt が余白を再分配する場合があるため。
    m_fxLayout->setSpacing(0);
    m_fxLayout->setSizeConstraint(QLayout::SetNoConstraint);
    m_fxContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    if (m_track) {
        connect(m_track, &Track::propertyChanged, this, &MixerChannelWidget::updateFxSlots);
    }
    updateFxSlots();
    
    layout->addWidget(m_fxContainer);

    // Knobs Area (Bottom Box)
    QWidget *knobsContainer = new QWidget(this);
    knobsContainer->setObjectName("knobsContainer");
    knobsContainer->setFixedHeight(76); // increased height for labels
    knobsContainer->setStyleSheet(QString(
        "#knobsContainer {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 4px;"
        "}"
    ).arg(Darwin::ThemeManager::instance().panelBackgroundColor().name(),
          Darwin::ThemeManager::instance().borderColor().name()));
    
    QHBoxLayout *knobsLayout = new QHBoxLayout(knobsContainer);
    knobsLayout->setContentsMargins(4, 4, 4, 4);
    knobsLayout->setSpacing(4);
    knobsLayout->setAlignment(Qt::AlignHCenter);
    
    // Timing Knob + Label
    QWidget* timingBlock = new QWidget(knobsContainer);
    QVBoxLayout* timingBlockLayout = new QVBoxLayout(timingBlock);
    timingBlockLayout->setContentsMargins(0,0,0,0);
    timingBlockLayout->setSpacing(2);
    KnobWidget *knobTiming = new KnobWidget("TIMING", timingBlock);
    // モデルからタイミングオフセット値を初期化（-100〜+100ms → ノブ0〜1に変換）
    if (m_track) {
        float initKnobVal = static_cast<float>((m_track->timingOffsetMs() + 100.0) / 200.0);
        knobTiming->setValue(qBound(0.0f, initKnobVal, 1.0f));
    } else {
        knobTiming->setValue(0.5f); // 0.5 = 中央 (0ms)
    }
    QLabel* timingValueLabel = new QLabel("0.0ms", timingBlock);
    timingValueLabel->setObjectName("timingValueLabel");
    timingValueLabel->setAlignment(Qt::AlignCenter);
    timingValueLabel->setStyleSheet(QString("font-size: 8px; color: %1;")
        .arg(Darwin::ThemeManager::instance().secondaryTextColor().name()));
    timingBlockLayout->addWidget(knobTiming);
    timingBlockLayout->addWidget(timingValueLabel);
    // ノブ操作 → モデルへ接続（0〜1 → -100〜+100ms）
    connect(knobTiming, &KnobWidget::valueChanged, [this, timingValueLabel](float val){
        float ms = (val - 0.5f) * 200.0f;
        timingValueLabel->setText(QString::number(ms, 'f', 1) + "ms");
        if (m_track) {
            m_track->setTimingOffsetMs(static_cast<double>(ms));
        }
    });
    
    // Pan Knob + Label
    QWidget* panBlock = new QWidget(knobsContainer);
    QVBoxLayout* panBlockLayout = new QVBoxLayout(panBlock);
    panBlockLayout->setContentsMargins(0,0,0,0);
    panBlockLayout->setSpacing(2);
    KnobWidget *knobPan = new KnobWidget("PAN", panBlock);
    QLabel* panValueLabel = new QLabel("C", panBlock);
    panValueLabel->setObjectName("panValueLabel");
    panValueLabel->setAlignment(Qt::AlignCenter);
    panValueLabel->setStyleSheet(QString("font-size: 8px; color: %1;")
        .arg(Darwin::ThemeManager::instance().secondaryTextColor().name()));
    
    if (m_track) {
        // pan is -1.0 to 1.0 (L to R), knob is 0.0 to 1.0
        knobPan->setValue((m_track->pan() + 1.0f) * 0.5f);
        connect(knobPan, &KnobWidget::valueChanged, m_track, [this](float val){
            m_track->setPan((val * 2.0f) - 1.0f);
        });
    } else {
        knobPan->setValue(0.5f); // Center
    }

    panBlockLayout->addWidget(knobPan);
    panBlockLayout->addWidget(panValueLabel);
    
    auto updatePanLabel = [panValueLabel](float val) {
        float panVal = (val * 2.0f) - 1.0f; // -1 to 1
        if (qAbs(panVal) < 0.01f) {
            panValueLabel->setText("C");
        } else if (panVal < 0) {
            panValueLabel->setText(QString("L%1").arg(static_cast<int>(-panVal * 100)));
        } else {
            panValueLabel->setText(QString("R%1").arg(static_cast<int>(panVal * 100)));
        }
    };
    // Initialize label
    updatePanLabel(knobPan->value());
    connect(knobPan, &KnobWidget::valueChanged, updatePanLabel);

    knobsLayout->addWidget(timingBlock);
    knobsLayout->addWidget(panBlock);

    layout->addWidget(knobsContainer);

    // Initial label text
    timingValueLabel->setText("0.0ms");

    // Track Name
    QString trName = m_track ? m_track->name() : trackName;
    QString labelText = isMaster ? "MASTER" : trName;
    
    if (isFolder) {
        // フォルダトラック: シェブロン + 名前 + 子トラック数を表示
        QWidget* folderNameArea = new QWidget(this);
        QVBoxLayout* folderNameLayout = new QVBoxLayout(folderNameArea);
        folderNameLayout->setContentsMargins(0, 0, 0, 0);
        folderNameLayout->setSpacing(2);

        // シェブロン + フォルダ名を横並び
        QWidget* chevronRow = new QWidget(folderNameArea);
        QHBoxLayout* chevronRowLayout = new QHBoxLayout(chevronRow);
        chevronRowLayout->setContentsMargins(0, 0, 0, 0);
        chevronRowLayout->setSpacing(2);

        // 展開/折りたたみシェブロンボタン
        IconButton* chevronBtn = new IconButton(IconButton::Chevron, chevronRow);
        chevronBtn->setCheckable(true);
        chevronBtn->setInitialFolderState(m_track->isFolderExpanded());
        chevronBtn->setStyleSheet(
            "QPushButton { border: none; background: transparent; }"
            "QPushButton:hover { background: rgba(0,0,0,0.06); border-radius: 4px; }"
        );
        connect(chevronBtn, &QPushButton::clicked, this, [this]() {
            emit folderToggleRequested(m_track);
        });
        chevronRowLayout->addWidget(chevronBtn);

        // フォルダ名ラベル
        QLabel* nameLabel = new QLabel(trName, chevronRow);
        nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        CustomTooltip::attach(nameLabel);
        nameLabel->setStyleSheet(QString(
            "font-family: %2;"
            "font-size: 10px;"
            "font-weight: 800;"
            "color: %1;"
            "letter-spacing: 0.5px;"
        ).arg(m_track->color().darker(140).name(),
              Darwin::FontManager::uiFontCss()));
        chevronRowLayout->addWidget(nameLabel, 1);

        folderNameLayout->addWidget(chevronRow);

        layout->addWidget(folderNameArea);
    } else {
        // 通常トラック / マスター
        QLabel *nameLabel = new QLabel(labelText, this);
        nameLabel->setObjectName("trackNameLabel");
        nameLabel->setAlignment(Qt::AlignCenter);
        CustomTooltip::attach(nameLabel);
        nameLabel->setStyleSheet(QString(
            "font-family: %2;"
            "font-size: 10px;"
            "font-weight: 800;"
            "color: %1;"
            "letter-spacing: 0.5px;"
            "margin-top: 4px;"
        ).arg(Darwin::ThemeManager::instance().textColor().name(),
              Darwin::FontManager::uiFontCss()));
        
        layout->addWidget(nameLabel);
    }
}

void MixerChannelWidget::applyTheme()
{
    bool isFolder = m_track && m_track->isFolder();

    // Fader Section
    QFrame* faderSection = findChild<QFrame*>("faderSection");
    if (faderSection) {
        if (!isFolder) {
            faderSection->setStyleSheet(QString(
                "#faderSection {"
                "  background-color: %1;"
                "  border: 1px solid %2;"
                "  border-radius: 12px;"
                "}"
            ).arg(Darwin::ThemeManager::instance().panelBackgroundColor().name(),
                  Darwin::ThemeManager::instance().borderColor().name()));
        } else {
            QColor baseColor = m_track->color();
            QColor bg = Darwin::ThemeManager::instance().panelBackgroundColor();
            QColor lightCol(
                (baseColor.red() * 15 + bg.red() * 85) / 100,
                (baseColor.green() * 15 + bg.green() * 85) / 100,
                (baseColor.blue() * 15 + bg.blue() * 85) / 100
            );
            QColor lightBorder(
                (baseColor.red() * 30 + bg.red() * 70) / 100,
                (baseColor.green() * 30 + bg.green() * 70) / 100,
                (baseColor.blue() * 30 + bg.blue() * 70) / 100
            );
            faderSection->setStyleSheet(QString(
                "#faderSection {"
                "  background-color: %1;"
                "  border: 1px solid %2;"
                "  border-radius: 12px;"
                "}"
            ).arg(lightCol.name(), lightBorder.name()));
        }
    }

    // Knobs Container
    QWidget* knobsContainer = findChild<QWidget*>("knobsContainer");
    if (knobsContainer) {
        knobsContainer->setStyleSheet(QString(
            "#knobsContainer {"
            "  background-color: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 4px;"
            "}"
        ).arg(Darwin::ThemeManager::instance().panelBackgroundColor().name(),
              Darwin::ThemeManager::instance().borderColor().name()));
    }

    // テキストラベルのテーマ更新
    const QString secColor = Darwin::ThemeManager::instance().secondaryTextColor().name();
    const QString textColor = Darwin::ThemeManager::instance().textColor().name();
    const QString uiFontCss = Darwin::FontManager::uiFontCss();
    if (QLabel* lbl = findChild<QLabel*>("trackNameLabel"))
        lbl->setStyleSheet(QString("font-family: %2; font-size: 10px; font-weight: 800; color: %1; letter-spacing: 0.5px; margin-top: 4px;").arg(textColor, uiFontCss));
    if (QLabel* lbl = findChild<QLabel*>("volLabel"))
        lbl->setStyleSheet(QString("font-family: %2; font-size: 9px; color: %1;").arg(secColor, uiFontCss));
    if (QLabel* lbl = findChild<QLabel*>("timingValueLabel"))
        lbl->setStyleSheet(QString("font-size: 8px; color: %1;").arg(secColor));
    if (QLabel* lbl = findChild<QLabel*>("panValueLabel"))
        lbl->setStyleSheet(QString("font-size: 8px; color: %1;").arg(secColor));
    // FX Slots are re-rendered based on state or we update them
    updateFxSlots();
}

void MixerChannelWidget::setLevel(float left, float right)
{
    if (m_levelMeterL) {
        m_levelMeterL->setLevel(left);
    }
    if (m_levelMeterR) {
        m_levelMeterR->setLevel(right);
    }
}

void MixerChannelWidget::updateFxSlots()
{
    const QFont fxButtonFont = Darwin::FontManager::uiFont(10, QFont::Bold);
    const QFontMetrics fxButtonMetrics(fxButtonFont);
    // macOS のシステムフォントは descender が深めなので、固定 22px だと下側が潰れやすい。
    const int fxButtonHeight = qMax(24, fxButtonMetrics.height() + 10);
    const int addFxButtonHeight = qMax(22, fxButtonMetrics.height() + 6);
    constexpr int kFxSpacing = 4; // ボタン間の固定スペース

    // 既存の子ボタンを全て削除（手動配置なのでレイアウトではなく children を操作）
    const auto oldButtons = m_fxContainer->findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly);
    for (QPushButton* b : oldButtons) {
        b->hide();
        b->deleteLater();
    }
    
    // Create buttons for each loaded FX
    if (m_track) {
        const auto& fxPlugins = m_track->fxPlugins();
        for (int i = 0; i < fxPlugins.size(); ++i) {
            VST3PluginInstance* fx = fxPlugins.at(i);
            QPushButton *btn = new QPushButton(fx->pluginName(), m_fxContainer);
            btn->setFont(fxButtonFont);
            btn->setFixedHeight(fxButtonHeight);
            btn->setCursor(Qt::PointingHandCursor);
            
            if (fx->isBypassed()) {
                QString bg = Darwin::ThemeManager::instance().isDarkMode() ? "#334155" : "#f1f5f9";
                QString fg = Darwin::ThemeManager::instance().isDarkMode() ? "#94a3b8" : "#94a3b8";
                QString border = Darwin::ThemeManager::instance().isDarkMode() ? "#475569" : "#cbd5e1";
                btn->setStyleSheet(QString(
                    "QPushButton {"
                    "  background-color: %1;" 
                    "  color: %2;" 
                    "  border: 1px solid %3;"
                    "  border-radius: 2px;"
                    "  padding: 1px 4px 2px 4px;"
                    "}"
                    "QPushButton:hover { background-color: %3; }"
                    "QPushButton:pressed { background-color: %3; color: %2; }"
                    "QPushButton:focus { outline: none; }"
                ).arg(bg, fg, border));
            } else {
                const auto& tm = Darwin::ThemeManager::instance();
                QString bg = tm.accentBgColor().name();
                QString fg = tm.accentTextColor().name();
                QString border = tm.accentBorderColor().name();
                btn->setStyleSheet(QString(
                    "QPushButton {"
                    "  background-color: %1;" 
                    "  color: %2;" 
                    "  border: 1px solid %3;"
                    "  border-radius: 2px;"
                    "  padding: 1px 4px 2px 4px;"
                    "}"
                    "QPushButton:hover { background-color: %3; }"
                    "QPushButton:pressed { background-color: %3; color: %2; }"
                    "QPushButton:focus { outline: none; }"
                ).arg(bg, fg, border));
            }
            
            // 手動 setGeometry() でボタン位置を確定する。
            // Qt layout エンジンを経由しないため activate() による再分配は起きない。
            const int btnY = i * (fxButtonHeight + kFxSpacing);
            const int btnW = qMax(1, m_fxContainer->width());
            btn->setGeometry(0, btnY, btnW, fxButtonHeight);
            btn->show();

            // 新規追加ボタン: 最初から opacity=0 にしておくことで、
            // animateFxAppear が呼ばれるまで非表示状態を維持する（hide() 不要＝点滅なし）。
            if (i == m_pendingFxHighlight) {
                auto* pendingEff = new QGraphicsOpacityEffect(btn);
                pendingEff->setOpacity(0.0);
                btn->setGraphicsEffect(pendingEff);
            }

            // Event filter for double-click removal and long-press bypass
            btn->setProperty("fxIndex", i);
            btn->installEventFilter(this);

            // Clicks request editor to open in Inspector
            connect(btn, &QPushButton::clicked, this, [this, fx, btn]() {
                if (btn->property("longPressTriggered").toBool()) {
                    btn->setProperty("longPressTriggered", false);
                    return;
                }
                emit pluginEditorRequested(fx);
            });
        }
    }

    // "+" ボタン
    const int fxCount = m_track ? m_track->fxPlugins().size() : 0;
    const int addY = fxCount * (fxButtonHeight + kFxSpacing);
    QPushButton *addFxBtn = new QPushButton("+", m_fxContainer);
    addFxBtn->setFont(fxButtonFont);
    addFxBtn->setCursor(Qt::PointingHandCursor);
    QString addBg = Darwin::ThemeManager::instance().isDarkMode() ? "#1e293b" : "#f8fafc";
    QString addHover = Darwin::ThemeManager::instance().isDarkMode() ? "#334155" : "#f1f5f9";
    QString addBorder = Darwin::ThemeManager::instance().isDarkMode() ? "#475569" : "#cbd5e1";
    QString addFg = Darwin::ThemeManager::instance().isDarkMode() ? "#94a3b8" : "#94a3b8";
    addFxBtn->setStyleSheet(QString(
        "QPushButton {"
        "  border: 1px dashed %3;"
        "  border-radius: 2px;"
        "  color: %4;"
        "  background-color: %1;"
        "  padding: 0px 4px 1px 4px;"
        "}"
        "QPushButton:hover { background-color: %2; color: #64748b; }"
    ).arg(addBg, addHover, addBorder, addFg));
    connect(addFxBtn, &QPushButton::clicked, this, &MixerChannelWidget::showFxPluginMenu);
    const int btnW = qMax(1, m_fxContainer->width());
    addFxBtn->setGeometry(0, addY, btnW, addFxButtonHeight);
    addFxBtn->show();

    // コンテナ高さを確定する（外側の VBoxLayout にサイズを伝えるため）
    const int totalFxHeight = fxCount * (fxButtonHeight + kFxSpacing) + addFxButtonHeight;
    m_fxContainer->setFixedHeight(totalFxHeight);
    // 外側レイアウトに高さ変化を通知する。
    // ここで activate() するのは外側 (MixerChannelWidget) の VBoxLayout であり、
    // m_fxContainer の内部には layout がないため spacing 再分配は起きない。
    if (layout()) layout()->activate();

    // 追加アニメーション
    // QTimer::singleShot(0) でイベントキューに積み、現在の propertyChanged シグナル処理
    // （PlaybackController::prepareAudio 等のブロッキング処理）が全て完了した後の
    // 次のイベントループサイクルでアニメーションを開始する。
    // 直接呼ぶと prepareAudio がメインスレッドをブロックする間にアニメーションが
    // 消費されてしまい、フェードインが視覚的に見えなくなるため。
    if (m_pendingFxHighlight >= 0) {
        const int indexToAnimate = m_pendingFxHighlight;
        m_pendingFxHighlight = -1;
        qDebug() << "[updateFxSlots] scheduling animateFxAppear for fxIndex=" << indexToAnimate;
        QTimer::singleShot(0, this, [this, indexToAnimate]() {
            animateFxAppear(indexToAnimate);
        });
    }
}

void MixerChannelWidget::showFxPluginMenu()
{
    if (!m_track) return;
    
    // プラグイン使用履歴を読み込むヘルパー
    auto loadUsageData = [](QVector<VST3PluginInfo>& plugins) {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Darwin", "PluginUsage");
        settings.beginGroup("UsageCount");
        for (auto& plugin : plugins) {
            QString key = QString(plugin.path.toUtf8().toBase64());
            plugin.usageCount = settings.value(key, 0).toInt();
        }
        settings.endGroup();
    };

    // プラグイン使用履歴を保存するヘルパー
    auto saveUsageData = [](const QString& pluginPath) {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Darwin", "PluginUsage");
        settings.beginGroup("UsageCount");
        QString key = QString(pluginPath.toUtf8().toBase64());
        int currentCount = settings.value(key, 0).toInt();
        settings.setValue(key, currentCount + 1);
        settings.endGroup();
        settings.sync();
    };

    if (!s_hasScanned) {
        // テーマに合わせたスキャンダイアログを作成
        bool isDark = Darwin::ThemeManager::instance().isDarkMode();
        QString dlgBg     = isDark ? "#1e293b" : "#ffffff";
        QString dlgText   = isDark ? "#e2e8f0" : "#1e293b";
        QString dlgBorder = isDark ? "#334155" : "#cbd5e1";
        QString dlgSec    = isDark ? "#94a3b8" : "#64748b";

        QProgressDialog* progress = new QProgressDialog("Scanning VST3 Plugins...", QString(), 0, 0, this);
        progress->setAttribute(Qt::WA_DeleteOnClose);
        progress->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(0);
        progress->setMinimumSize(280, 80);
        progress->setMaximumSize(280, 80);
        progress->setCancelButton(nullptr); // キャンセルボタン不要
        progress->setStyleSheet(QString(R"(
            QProgressDialog {
                background-color: %1;
                border: 1px solid %2;
                border-radius: 12px;
                padding: 16px;
            }
            QLabel {
                color: %3;
                font-family: %7;
                font-size: 12px;
                font-weight: 600;
                letter-spacing: 0.5px;
                background: transparent;
            }
            QProgressBar {
                border: 1px solid %2;
                border-radius: 4px;
                background-color: %4;
                text-align: center;
                color: %5;
                font-size: 10px;
                max-height: 6px;
            }
            QProgressBar::chunk {
                background-color: %6;
                border-radius: 3px;
            }
        )").arg(dlgBg, dlgBorder, dlgText, isDark ? "#0f172a" : "#f1f5f9", dlgSec, Darwin::ThemeManager::instance().accentColor().name(), Darwin::FontManager::uiFontCss()));
        progress->setValue(1); // Force show
        progress->show();

        if (!m_scanner) {
            m_scanner = new VST3Scanner(this);
        }
        
        // ワーカースレッドでスキャンを実行し、メインスレッドブロックを回避する。
        // 不正な DLL が LoadLibrary/DllMain でクラッシュしても
        // メインスレッドには直接影響しない。
        QFutureWatcher<QVector<VST3PluginInfo>>* watcher =
            new QFutureWatcher<QVector<VST3PluginInfo>>(this);

        connect(watcher, &QFutureWatcher<QVector<VST3PluginInfo>>::finished,
                this, [this, watcher, progress, loadUsageData, saveUsageData]() {
            s_cachedPlugins = watcher->result();
            s_hasScanned = true;
            progress->close();

            // スキャン完了後にメニューを表示
            showFxPluginMenuAfterScan(loadUsageData, saveUsageData);

            watcher->deleteLater();
        });

        QFuture<QVector<VST3PluginInfo>> future = QtConcurrent::run([this]() {
            return m_scanner->scan(false);
        });
        watcher->setFuture(future);
        return; // スキャン完了を待ってからメニュー表示
    }

    showFxPluginMenuAfterScan(loadUsageData, saveUsageData);
}

void MixerChannelWidget::showFxPluginMenuAfterScan(
    std::function<void(QVector<VST3PluginInfo>&)> loadUsageData,
    std::function<void(const QString&)> saveUsageData)
{
    if (!m_track) return;

    // メニュー表示のたびに最新の利用回数を反映してソート
    loadUsageData(s_cachedPlugins);
    std::sort(s_cachedPlugins.begin(), s_cachedPlugins.end(), [](const VST3PluginInfo& a, const VST3PluginInfo& b) {
        if (a.usageCount != b.usageCount) {
            return a.usageCount > b.usageCount;
        }
        return a.name.toLower() < b.name.toLower();
    });

    QMenu* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    {
        bool isDark = Darwin::ThemeManager::instance().isDarkMode();
        QString menuBg     = isDark ? "#1e293b" : "#ffffff";
        QString menuFg     = isDark ? "#e2e8f0" : "#1e293b";
        QString menuBorder = isDark ? "#334155" : "#cbd5e1";
        QString menuSel    = isDark ? "#2d3748" : "#e2e8f0";
        menu->setStyleSheet(QString(
            "QMenu { "
            "  background-color: %1; "
            "  color: %2; "
            "  border: 1px solid %3; "
            "  border-radius: 4px; "
            "} "
            "QMenu::item {"
            "  padding: 6px 24px 6px 16px;"
            "} "
            "QMenu::item:selected {"
            "  background-color: %4;"
            "}"
        ).arg(menuBg, menuFg, menuBorder, menuSel));
    }
    
    for (const auto& info : s_cachedPlugins) {
        if (info.isEffect) {
            // Set a fallback name if empty
            QString displayName = info.name.trimmed();
            if (displayName.isEmpty()) {
                displayName = QFileInfo(info.path).baseName();
            }
            if (displayName.isEmpty()) {
                displayName = "Unknown FX";
            }
            
            QAction* action = menu->addAction(displayName);
            connect(action, &QAction::triggered, this, [this, info, saveUsageData]() {
                // 利用回数をインクリメントして保存
                saveUsageData(info.path);
                
                // 次のメニュー表示時にソート結果を反映させるためキャッシュ側の一時カウントも更新
                // （厳密には次回メニュー表示時に file から再ロードされるため不要だが安全のため）
                for (auto& cached : s_cachedPlugins) {
                    if (cached.path == info.path) {
                        cached.usageCount++;
                        break;
                    }
                }

                // FX追加前のインデックスを記録
                m_pendingFxHighlight = m_track->fxPlugins().size();
                m_track->addFxPlugin(info.path);
            });
        }
    }
    
    if (menu->isEmpty()) {
        QAction* emptyAct = menu->addAction("No FX Plugins Found");
        emptyAct->setEnabled(false);
    }
    
    menu->exec(QCursor::pos());
}

bool MixerChannelWidget::eventFilter(QObject *watched, QEvent *event)
{
    // m_fxContainer がリサイズされたらボタン幅を追従させる
    if (watched == m_fxContainer && event->type() == QEvent::Resize) {
        const int w = m_fxContainer->width();
        for (QPushButton* b : m_fxContainer->findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly)) {
            if (b->width() != w) b->resize(w, b->height());
        }
        return false;
    }

    QPushButton* btn = qobject_cast<QPushButton*>(watched);
    if (!btn) return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonDblClick) {
        bool ok;
        int idx = btn->property("fxIndex").toInt(&ok);
        if (ok && m_track) {
            // エディタが開いている場合は先に閉じる（use-after-free防止）
            emit pluginEditorRequested(nullptr);
            // はじけ飛ぶアニメーションを再生してから削除
            animateFxBurst(btn);
            // 削除はアニメーション後に実行
            QTimer::singleShot(250, this, [this, idx]() {
                if (m_track && idx < m_track->fxPlugins().size()) {
                    m_track->removeFxPlugin(idx);
                }
            });
            return true;
        }
    } else if (event->type() == QEvent::MouseMove) {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        if (me->buttons() & Qt::LeftButton) {
            qint64 pressTime = btn->property("pressTime").toLongLong();
            if (pressTime > 0) {
                // Not a long press trigger, but a drag trigger
                QPoint pressPos = btn->property("pressPos").toPoint();
                if ((me->pos() - pressPos).manhattanLength() > QApplication::startDragDistance()) {
                    bool ok;
                    int idx = btn->property("fxIndex").toInt(&ok);
                    if (ok && m_track) {
                        btn->setProperty("pressTime", 0);
                        beginFxDrag(btn, idx);
                        return true;
                    }
                }
            }
        }
    } else if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            btn->setProperty("pressTime", QDateTime::currentMSecsSinceEpoch());
            btn->setProperty("pressPos", me->pos());
            btn->setProperty("longPressTriggered", false);
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            qint64 pressTime = btn->property("pressTime").toLongLong();
            if (pressTime > 0 && (QDateTime::currentMSecsSinceEpoch() - pressTime > 400)) {
                // It's a long press
                btn->setProperty("longPressTriggered", true);
                bool ok;
                int idx = btn->property("fxIndex").toInt(&ok);
                if (ok && m_track) {
                    if (idx >= 0 && idx < m_track->fxPlugins().size()) {
                        VST3PluginInstance* p = m_track->fxPlugins().at(idx);
                        p->setBypassed(!p->isBypassed());
                        updateFxSlots();
                    }
                }
                // Do not return true, so the button visual state doesn't get stuck.
                // The clicked handler checks "longPressTriggered" and ignores it.
            }
            btn->setProperty("pressTime", 0);
        }
    }

    return QWidget::eventFilter(watched, event);
}

// ── FX Drag & Drop ───────────────────────────────────────────────

void MixerChannelWidget::beginFxDrag(QPushButton* btn, int index)
{
    if (!m_track || index < 0 || index >= m_track->fxPlugins().size()) return;

    m_isDraggingFx = true;
    m_dragFxPlugin = m_track->fxPlugins().at(index);
    m_dragInsertIndex = index;

    // 半透明にする
    auto* dimEffect = new QGraphicsOpacityEffect(btn);
    dimEffect->setOpacity(0.3);
    btn->setGraphicsEffect(dimEffect);

    // ゴーストを作成
    m_dragGhost = new QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
    m_dragGhost->setAttribute(Qt::WA_TranslucentBackground);
    m_dragGhost->setFixedSize(btn->width(), btn->height());

    QPixmap pixmap(btn->size() * btn->devicePixelRatioF());
    pixmap.setDevicePixelRatio(btn->devicePixelRatioF());
    pixmap.fill(Qt::transparent);
    btn->render(&pixmap);

    QLabel* ghostLabel = new QLabel(m_dragGhost);
    ghostLabel->setPixmap(pixmap);
    ghostLabel->setFixedSize(btn->size());
    ghostLabel->move(0, 0);

    bool isDark = Darwin::ThemeManager::instance().isDarkMode();
    m_dragGhost->setStyleSheet(QString(
        "background-color: %1; border: 2px solid #3b82f6; border-radius: 2px;"
    ).arg(isDark ? "rgba(30,41,59,220)" : "rgba(241,245,249,230)"));

    QPoint btnGlobal = btn->mapToGlobal(QPoint(0, 0));
    m_dragOffset = QCursor::pos() - btnGlobal;
    m_dragGhost->move(QCursor::pos() - m_dragOffset);
    m_dragGhost->show();

    // 浮き上がりアニメーション
    m_dragGhost->setWindowOpacity(0.0);
    auto* floatAnim = new QPropertyAnimation(m_dragGhost, "windowOpacity", m_dragGhost);
    floatAnim->setDuration(200);
    floatAnim->setStartValue(0.0);
    floatAnim->setEndValue(0.92);
    floatAnim->setEasingCurve(QEasingCurve::OutCubic);
    floatAnim->start(QAbstractAnimation::DeleteWhenStopped);

    m_dropIndicator = new QWidget(m_fxContainer);
    m_dropIndicator->setFixedHeight(3);
    m_dropIndicator->setStyleSheet("background-color: #3b82f6; border-radius: 1px;");
    m_dropIndicator->hide();

    // QDrag 開始
    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData;
    
    // Trackポインタとインデックスをシリアライズ
    QByteArray encodedData;
    QDataStream stream(&encodedData, QIODevice::WriteOnly);
    stream << reinterpret_cast<quintptr>(m_track) << index;
    mimeData->setData("application/x-darwin-fx-slot", encodedData);
    drag->setMimeData(mimeData);

    // 空のピックスマップを設定してOS標準のドラッグアイコンを非表示にする
    QPixmap emptyPixmap(1, 1);
    emptyPixmap.fill(Qt::transparent);
    drag->setPixmap(emptyPixmap);

    // マウスを離すまでブロック
    drag->exec(Qt::MoveAction);

    // ドロップ完了またはキャンセル時
    finishFxDrag();
    btn->setGraphicsEffect(nullptr);
}

void MixerChannelWidget::finishFxDrag()
{
    m_isDraggingFx = false;
    m_dragFxPlugin = nullptr;
    m_dragInsertIndex = -1;

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

    if (m_dropIndicator) {
        m_dropIndicator->deleteLater();
        m_dropIndicator = nullptr;
    }
}

void MixerChannelWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-darwin-fx-slot") && m_track) {
        event->acceptProposedAction();
        if (!m_dropIndicator) {
            m_dropIndicator = new QWidget(m_fxContainer);
            m_dropIndicator->setFixedHeight(3);
            m_dropIndicator->setStyleSheet("background-color: #3b82f6; border-radius: 1px;");
            m_dropIndicator->hide();
        }
    } else {
        event->ignore();
    }
}

void MixerChannelWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-darwin-fx-slot")) {
        event->acceptProposedAction();

        // fxContainer 内でのローカル座標に変換
        QPoint localPos = m_fxContainer->mapFrom(this, event->position().toPoint());
        
        // ゴーストの更新 (別のMixerChannelへドラッグしている可能性もあるので、現在QDragをループ中の本人が親とは限らない)
        // 今回のゴースト実装はトップレベルにあるため、OS標準のカーソル位置からゴーストを動かすのはbegin側で行うか、あるいはQDrag中にタイマーで行う必要がある。
        // ここでは dropIndicator の位置のみ更新する

        int insertIndex = 0;
        int maxIndex = m_track->fxPlugins().size();
        
        QByteArray encodedData = event->mimeData()->data("application/x-darwin-fx-slot");
        QDataStream stream(&encodedData, QIODevice::ReadOnly);
        quintptr sourceTrackPtr;
        int sourceIndex;
        stream >> sourceTrackPtr >> sourceIndex;
        Track* sourceTrack = reinterpret_cast<Track*>(sourceTrackPtr);

        for (int i = 0; i < m_fxLayout->count() - 1; ++i) { // 最後の+ボタンは除く
            QLayoutItem* item = m_fxLayout->itemAt(i);
            if (item && item->widget()) {
                QRect geo = item->widget()->geometry();
                if (localPos.y() < geo.center().y()) {
                    break;
                }
                insertIndex = i + 1;
            }
        }
        
        // 挿入インジケーター表示
        if (m_dropIndicator) {
            int indicatorY = 0;
            if (insertIndex == 0) {
                indicatorY = 0;
            } else if (insertIndex <= maxIndex) {
                QLayoutItem* prevItem = m_fxLayout->itemAt(insertIndex - 1);
                if (prevItem && prevItem->widget()) {
                    indicatorY = prevItem->widget()->geometry().bottom() + m_fxLayout->spacing() / 2 - 1;
                }
            } else {
                 QLayoutItem* addBtnItem = m_fxLayout->itemAt(m_fxLayout->count() - 1);
                 if (addBtnItem && addBtnItem->widget()) {
                     indicatorY = addBtnItem->widget()->geometry().top() - m_fxLayout->spacing() / 2 - 1;
                 }
            }
            m_dropIndicator->setGeometry(0, indicatorY, m_fxContainer->width(), 3);
            m_dropIndicator->show();
            m_dropIndicator->raise();
        }
    }
}

void MixerChannelWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
    if (m_dropIndicator) {
        m_dropIndicator->hide();
    }
    event->accept();
}

void MixerChannelWidget::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-darwin-fx-slot") && m_track) {
        QByteArray encodedData = event->mimeData()->data("application/x-darwin-fx-slot");
        QDataStream stream(&encodedData, QIODevice::ReadOnly);
        quintptr sourceTrackPtr;
        int sourceIndex;
        stream >> sourceTrackPtr >> sourceIndex;

        Track* sourceTrack = reinterpret_cast<Track*>(sourceTrackPtr);
        if (!sourceTrack) {
            event->ignore();
            return;
        }

        QPoint localPos = m_fxContainer->mapFrom(this, event->position().toPoint());

        int insertIndex = 0;
        int maxIndex = m_track->fxPlugins().size();

        for (int i = 0; i < m_fxLayout->count() - 1; ++i) {
            QLayoutItem* item = m_fxLayout->itemAt(i);
            if (item && item->widget()) {
                QRect geo = item->widget()->geometry();
                if (localPos.y() < geo.center().y()) {
                    break;
                }
                insertIndex = i + 1;
            }
        }

        if (sourceTrack == m_track) {
            // 同一トラック内での移動
            int toIndex = insertIndex;
            if (toIndex > sourceIndex) {
                toIndex--; // 自分自身が抜ける分を調整
            }
            if (sourceIndex != toIndex) {
                m_track->moveFxPlugin(sourceIndex, toIndex);
            }
        } else {
            // トラック間での移動
            VST3PluginInstance* fx = sourceTrack->takeFxPlugin(sourceIndex);
            if (fx) {
                m_track->insertFxPlugin(insertIndex, fx);
            }
        }

        event->acceptProposedAction();
    } else {
        event->ignore();
    }
    
    if (m_dropIndicator) {
        m_dropIndicator->hide();
    }
}

// ── Burst animation on FX delete ──────────────────────────────
void MixerChannelWidget::animateFxBurst(QPushButton* btn)
{
    if (!btn) return;

    // Opacity fade-out
    auto* eff = new QGraphicsOpacityEffect(btn);
    btn->setGraphicsEffect(eff);

    auto* fadeAnim = new QPropertyAnimation(eff, "opacity", btn);
    fadeAnim->setDuration(250);
    fadeAnim->setStartValue(1.0);
    fadeAnim->setEndValue(0.0);
    fadeAnim->setEasingCurve(QEasingCurve::OutCubic);
    fadeAnim->start(QAbstractAnimation::DeleteWhenStopped);

    // Scale-up effect via stylesheet max-height trickery:
    // We animate the button geometry to expand slightly from center
    QRect startGeo = btn->geometry();
    QRect endGeo = startGeo.adjusted(-6, -4, 6, 4);

    auto* geoAnim = new QPropertyAnimation(btn, "geometry", btn);
    geoAnim->setDuration(250);
    geoAnim->setStartValue(startGeo);
    geoAnim->setEndValue(endGeo);
    geoAnim->setEasingCurve(QEasingCurve::OutCubic);
    geoAnim->start(QAbstractAnimation::DeleteWhenStopped);

    // Brief color flash before disappearing
    QString origStyle = btn->styleSheet();
    btn->setStyleSheet(origStyle + " background-color: rgba(255,120,80,200); border: 1px solid rgba(255,180,100,220);");
    QTimer::singleShot(120, btn, [btn, origStyle]() {
        if (btn) btn->setStyleSheet(origStyle);
    });
}

// ── Fade-in animation on FX add ──────────────────────────────
void MixerChannelWidget::animateFxAppear(int fxIndex, int retryCount)
{
    // prepareAudio はUIタイマー(50ms)から非同期で呼ばれる。
    // 完了前にアニメーションを開始するとメインスレッドのブロック中に全フレームが消費されてしまう。
    // isAudioPrepared() がtrueになるまで30ms間隔でリトライ（最大40回 = 1.2秒）。
    if (m_track) {
        const auto& fxList = m_track->fxPlugins();
        if (fxIndex < fxList.size()) {
            VST3PluginInstance* fx = fxList.value(fxIndex);
            if (fx && !fx->isAudioPrepared() && retryCount < 40) {
                if (retryCount == 0)
                    qDebug() << "[animateFxAppear] fxIndex=" << fxIndex << " waiting for prepareAudio...";
                QTimer::singleShot(30, this, [this, fxIndex, retryCount]() {
                    animateFxAppear(fxIndex, retryCount + 1);
                });
                return;
            }
            qDebug() << "[animateFxAppear] fxIndex=" << fxIndex
                     << " retryCount=" << retryCount
                     << " isAudioPrepared=" << (fx ? fx->isAudioPrepared() : false);
        } else {
            qDebug() << "[animateFxAppear] fxIndex=" << fxIndex << " out of range, fxList.size()=" << fxList.size();
        }
    }

    // 手動配置に変更したため m_fxLayout は使わず、fxIndex プロパティで検索する
    QPushButton* btn = nullptr;
    for (QPushButton* b : m_fxContainer->findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly)) {
        bool ok;
        if (b->property("fxIndex").toInt(&ok) == fxIndex && ok) {
            btn = b;
            break;
        }
    }
    if (!btn) { qDebug() << "[animateFxAppear] btn NOT FOUND for fxIndex=" << fxIndex; return; }

    // prepareAudio 完了後に burst 発火した複数のリトライが全て通過するのを防ぐ。
    // 同一ボタンへの 2 回目以降の呼び出しは無視する。
    if (btn->property("animScheduled").toBool()) {
        qDebug() << "[animateFxAppear] already scheduled, skip";
        return;
    }
    btn->setProperty("animScheduled", true);
    btn->raise();

    qDebug() << "[animateFxAppear] btn=" << btn->text() << " scheduling 300ms post-prepare delay";

    // prepareAudio 直後はプラグインの post-load 処理（restartComponent 等）が続くため、
    // 300ms 待機してからアニメーションを開始する。
    QTimer::singleShot(300, btn, [btn]() {
        qDebug() << "[animateFxAppear] starting frame-based animation";
        auto* eff = qobject_cast<QGraphicsOpacityEffect*>(btn->graphicsEffect());
        if (!eff) {
            eff = new QGraphicsOpacityEffect(btn);
            btn->setGraphicsEffect(eff);
        }
        eff->setOpacity(0.0);

        // ── フレームベースアニメーション ────────────────────────────────────
        // QPropertyAnimation は「経過実時間ベース」のため、メインスレッドが
        // ブロックされてタイマーが溜まると一気に進んで爆速に見える。
        // QTimer + 固定ステップ方式なら、溜まったタイマーが burst 発火しても
        // 各発火は 1 フレーム分しか進まないためジャンプしない。
        constexpr int kFrames = 50;  // 50 × 16ms ≈ 800ms
        auto frame = std::make_shared<int>(0);
        auto* timer = new QTimer(btn);
        timer->setInterval(16);
        connect(timer, &QTimer::timeout, btn, [eff, frame, timer, btn]() {
            if (++(*frame) >= kFrames) {
                eff->setOpacity(1.0);
                btn->setGraphicsEffect(nullptr);
                timer->stop();
                timer->deleteLater();
                return;
            }
            const double t = (double)(*frame) / kFrames;
            const double s = 1.0 - t;
            eff->setOpacity(1.0 - s * s * s);  // OutCubic
        });
        timer->start();
        qDebug() << "[animateFxAppear] frame timer started (50 frames x 16ms)";
    });
}
