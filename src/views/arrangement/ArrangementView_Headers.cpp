/**
 * @file ArrangementView_Headers.cpp
 * @brief トラック/フォルダヘッダーの生成ロジック
 */
#include "ArrangementView.h"
#include "ArrangementGridWidget.h"
#include "widgets/IconButton.h"
#include "widgets/CustomTooltip.h"
#include "models/Project.h"
#include "models/Track.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QColorDialog>
#include "common/Constants.h"
#include "common/ThemeManager.h"
// 名前編集フィールドのスタイルをテーマに合わせて生成
static QString makeNameEditStyle(const Darwin::ThemeManager& tm, int fontSize = 11)
{
    return QString(
        "QLineEdit { font-family: 'Segoe UI', sans-serif; font-size: %1px; font-weight: 700;"
        " color: %2; background: transparent; border: 1px solid transparent;"
        " border-radius: 2px; padding: 2px; }"
        "QLineEdit:hover { border: 1px solid %3; }"
        "QLineEdit:focus { background: %4; border: 1px solid %5; }"
    ).arg(fontSize)
     .arg(tm.textColor().name())
     .arg(tm.borderColor().name())
     .arg(tm.panelBackgroundColor().name())
     .arg(tm.isDarkMode() ? "#94a3b8" : "#3b82f6");
}
// ─── 静的ヘルパー ────────────────────────────────────────

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
    // ネストされたトラックの場合、親フォルダから色を取得
    if (isChild) {
        Track* parentFolder = project->trackById(t->parentFolderId());
        if (parentFolder) {
            QColor fColor = parentFolder->color();
            int alpha = isSelected ? 40 : 15;
            if (isFolder) {
                alpha = isSelected ? 60 : 30;
            }
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
}

// ─── フォルダヘッダー生成 ────────────────────────────────

QWidget* ArrangementView::createFolderHeader(Track* folder)
{
    QWidget *trackWidget = new QWidget();
    trackWidget->setObjectName("folderHeader");
    trackWidget->setFixedHeight(Darwin::ARRANGEMENT_TRACK_ROW_HEIGHT);
    
    applyTrackHeaderStyle(trackWidget, folder, m_selectedTrack, m_project);
    
    trackWidget->installEventFilter(this);
    trackWidget->setProperty("track_ptr", QVariant::fromValue<void*>(folder));
    
    QHBoxLayout *tLayout = new QHBoxLayout(trackWidget);
    tLayout->setContentsMargins(8, 0, 8, 0);

    // フォルダアイコン (展開/折りたたみ兼用)
    IconButton *folderIcon = new IconButton(IconButton::Folder, trackWidget);
    folderIcon->setObjectName("folderExpandBtn");
    folderIcon->setCheckable(true);
    folderIcon->setInitialFolderState(folder->isFolderExpanded());
    {
        bool isDark = Darwin::ThemeManager::instance().isDarkMode();
        folderIcon->setStyleSheet(QString(
            "QPushButton { border: none; background: transparent; }"
            "QPushButton:hover { background: %1; border-radius: 4px; }"
        ).arg(isDark ? "#2d3748" : "#e2e8f0"));
    }
    connect(folderIcon, &QPushButton::clicked, this, [this, folder, folderIcon]() {
        folder->setFolderExpanded(folderIcon->isChecked());
        rebuildHeaders();
    });

    // フォルダカラースウォッチ
    QPushButton *colorBtn = new QPushButton(trackWidget);
    colorBtn->setFixedSize(20, 20);
    colorBtn->setCursor(Qt::PointingHandCursor);
    auto updateColorBtnStyle = [colorBtn](const QColor& c) {
        colorBtn->setStyleSheet(QString(
            "QPushButton { background-color: %1; border: 2px solid %2; border-radius: 4px; }"
            "QPushButton:hover { border: 2px solid #333; }"
        ).arg(c.name(), c.darker(120).name()));
    };
    updateColorBtnStyle(folder->color());
    connect(colorBtn, &QPushButton::clicked, this, [this, folder, colorBtn, updateColorBtnStyle]() {
        QColor chosen = QColorDialog::getColor(folder->color(), colorBtn, "フォルダカラーを選択");
        if (chosen.isValid()) {
            folder->setColor(chosen);
            updateColorBtnStyle(chosen);
            
            // 自身の色更新
            applyTrackHeaderStyle(m_trackHeaders[folder], folder, m_selectedTrack, m_project);
            
            // 子トラックの背景色も同時に更新
            if (m_project) {
                for (Track* child : m_project->folderChildren(folder)) {
                    if (m_trackHeaders.contains(child)) {
                        applyTrackHeaderStyle(m_trackHeaders[child], child, m_selectedTrack, m_project);
                    }
                }
                rebuildHeaders(); // 全体の色と線を再構築
            }
        }
    });
    
    QLineEdit *nameEdit = new QLineEdit(folder->name(), trackWidget);
    nameEdit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    nameEdit->setCursorPosition(0);
    CustomTooltip::attach(nameEdit);
    nameEdit->setStyleSheet(makeNameEditStyle(Darwin::ThemeManager::instance(), 12));
    connect(nameEdit, &QLineEdit::editingFinished, folder, [folder, nameEdit]() {
        folder->setName(nameEdit->text());
        nameEdit->clearFocus();
        nameEdit->setCursorPosition(0);
    });
    
    IconButton *delBtn = new IconButton(IconButton::Trash, trackWidget);
    delBtn->setObjectName("delBtn");
    {
        bool isDark = Darwin::ThemeManager::instance().isDarkMode();
        delBtn->setStyleSheet(QString(
            "QPushButton { border: none; background: transparent; }"
            "QPushButton:hover { background: %1; border-radius: 4px; }"
        ).arg(isDark ? "#3d1515" : "#fee2e2"));
    }
    connect(delBtn, &QPushButton::clicked, this, [this, folder]() {
        if (!m_project) return;
        // フォルダ内のトラックを親に上げる
        int parentId = folder->parentFolderId();
        for (Track* child : m_project->folderChildren(folder)) {
            child->setParentFolderId(parentId);
        }
        m_project->removeTrack(folder);
    });
    
    tLayout->addWidget(folderIcon, 0);
    tLayout->addWidget(colorBtn, 0);
    tLayout->addWidget(nameEdit, 1);
    tLayout->addWidget(delBtn, 0);
    
    return trackWidget;
}

// ─── トラックヘッダー生成 ────────────────────────────────

QWidget* ArrangementView::createTrackHeader(Track* track)
{
    QWidget *trackWidget = new QWidget();
    trackWidget->setObjectName("trackHeader");

    trackWidget->setFixedHeight(Darwin::ARRANGEMENT_TRACK_ROW_HEIGHT); // グリッドのrowHeightと同期
    
    applyTrackHeaderStyle(trackWidget, track, m_selectedTrack, m_project);
    
    trackWidget->installEventFilter(this);
    trackWidget->setProperty("track_ptr", QVariant::fromValue<void*>(track));
    
    QHBoxLayout *tLayout = new QHBoxLayout(trackWidget);
    tLayout->setContentsMargins(8, 0, 16, 0);
    tLayout->setSpacing(4);
    
    QLineEdit *nameEdit = new QLineEdit(track->name());
    nameEdit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    nameEdit->setCursorPosition(0);
    CustomTooltip::attach(nameEdit);
    nameEdit->setStyleSheet(makeNameEditStyle(Darwin::ThemeManager::instance()));
    connect(nameEdit, &QLineEdit::editingFinished, track, [track, nameEdit]() {
        track->setName(nameEdit->text());
        nameEdit->clearFocus();
        nameEdit->setCursorPosition(0);
    });
    
    IconButton *eyeBtn = new IconButton(IconButton::Eye, trackWidget);
    eyeBtn->setObjectName("eyeBtn");
    eyeBtn->setCheckable(true);
    eyeBtn->setChecked(track->isVisible());
    {
        bool isDark = Darwin::ThemeManager::instance().isDarkMode();
        eyeBtn->setStyleSheet(QString(
            "QPushButton { border: none; background: transparent; }"
            "QPushButton:hover { background: %1; border-radius: 4px; }"
        ).arg(isDark ? "#3d1515" : "#fee2e2"));
    }
    
    IconButton *delBtn = new IconButton(IconButton::Trash, trackWidget);
    delBtn->setObjectName("delBtn");
    {
        bool isDark = Darwin::ThemeManager::instance().isDarkMode();
        delBtn->setStyleSheet(QString(
            "QPushButton { border: none; background: transparent; }"
            "QPushButton:hover { background: %1; border-radius: 4px; }"
        ).arg(isDark ? "#3d1515" : "#fee2e2"));
    }
    
    connect(eyeBtn, &QPushButton::toggled, track, &Track::setVisible);
    connect(delBtn, &QPushButton::clicked, this, [this, track]() {
        if (m_project) m_project->removeTrack(track);
    });
    
    // カラースウォッチボタン
    QPushButton *colorBtn = new QPushButton(trackWidget);
    colorBtn->setFixedSize(20, 20);
    colorBtn->setCursor(Qt::PointingHandCursor);
    auto updateColorBtnStyle = [colorBtn](const QColor& c) {
        colorBtn->setStyleSheet(QString(
            "QPushButton { background-color: %1; border: 2px solid %2; border-radius: 4px; }"
            "QPushButton:hover { border: 2px solid #333; }"
        ).arg(c.name(), c.darker(120).name()));
    };
    updateColorBtnStyle(track->color());
    
    connect(colorBtn, &QPushButton::clicked, this, [track, colorBtn, updateColorBtnStyle]() {
        QColor chosen = QColorDialog::getColor(track->color(), colorBtn, "トラックカラーを選択");
        if (chosen.isValid()) {
            track->setColor(chosen);
            updateColorBtnStyle(chosen);
        }
    });

    tLayout->addWidget(colorBtn, 0);
    tLayout->addWidget(nameEdit, 1);
    tLayout->addWidget(eyeBtn, 0);
    tLayout->addWidget(delBtn, 0);
    
    return trackWidget;
}
