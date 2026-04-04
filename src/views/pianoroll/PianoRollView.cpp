#include "PianoRollView.h"
#include "PianoRollGridWidget.h"
#include "VelocityLaneWidget.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include "common/FontManager.h"
#include "common/ThemeManager.h"

PianoRollView::PianoRollView(QWidget *parent) 
    : QWidget(parent)
    , m_project(nullptr)
    , m_grid(nullptr)
    , m_velocityLane(nullptr)
    , m_keysScrollArea(nullptr)
    , m_gridScrollArea(nullptr)
    , m_velocityScrollArea(nullptr)
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0,0,0,0);
    mainLayout->setSpacing(0);
    const QString monoFontCss = Darwin::FontManager::monoFontCss();

    // Left Side: Keys Area
    m_keysScrollArea = new QScrollArea(this);
    m_keysScrollArea->setWidgetResizable(true);
    m_keysScrollArea->setFrameShape(QFrame::NoFrame);
    m_keysScrollArea->setFixedWidth(48); // w-12
    m_keysScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_keysScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // Hide scrollbar, controlled by grid
    
    QWidget *keysWidget = new QWidget(m_keysScrollArea);
    m_keysWidget = keysWidget;
    {
        const Darwin::ThemeManager& tm = Darwin::ThemeManager::instance();
        const bool isDark = tm.isDarkMode();
        const QString kwBg     = isDark ? tm.panelBackgroundColor().name() : "#ffffff";
        const QString kwBorder = isDark ? tm.borderColor().name() : "#cccccc";
        keysWidget->setStyleSheet(QString("background-color: %1; border-right: 1px solid %2;").arg(kwBg, kwBorder));
    }
    
    QVBoxLayout *keysLayout = new QVBoxLayout(keysWidget);
    keysLayout->setContentsMargins(0,0,0,0);
    keysLayout->setSpacing(0);
    
    // Add keys (127 down to 0)
    QStringList noteNames = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    for(int i = 127; i >= 0; --i) {
        int octave = (i / 12) - 1; // MIDI Note 60 is C3 (60/12 - 1 = 4? Wait. Usually 60 is C3 or C4. Let's use standard assumption 60=C3. So 60/12=5, 5-2=3. octave = (i/12)-2)
        int noteIndex = i % 12;
        QString noteName = noteNames[noteIndex] + QString::number((i / 12) - 1); // C-1 to G9
        
        QLabel *key = new QLabel(noteName, keysWidget);
        key->setFixedHeight(12); // matching ROW_HEIGHT
        key->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        key->setIndent(8);
        
        bool isSharp = noteName.contains("#");
        {
            const Darwin::ThemeManager& tm2 = Darwin::ThemeManager::instance();
            const bool isDark = tm2.isDarkMode();
            const QString keyBorder = isDark ? tm2.borderColor().name() : "#f1f5f9";
            if (isSharp) {
                const QString sharpBg   = isDark ? tm2.pianoBlackKeyColor().name() : "#e2e8f0";
                const QString sharpText = isDark ? "#94a3b8" : "#333333";
                key->setStyleSheet(QString("background-color: %1; border-bottom: 1px solid %2; font-family: %4; font-size: 10px; color: %3; font-weight: 500;")
                    .arg(sharpBg, keyBorder, sharpText, monoFontCss));
            } else {
                const QString whiteBg   = isDark ? tm2.panelBackgroundColor().name() : "#f8fafc";
                const QString whiteText = isDark ? "#475569" : "#333333";
                key->setStyleSheet(QString("background-color: %1; border-bottom: 1px solid %2; font-family: %4; font-size: 10px; color: %3; font-weight: 500;")
                    .arg(whiteBg, keyBorder, whiteText, monoFontCss));
            }
        }
        
        if(noteName == "C3") { // Emphasize C3
             key->setStyleSheet(key->styleSheet() + "color: #FF3366; font-weight: bold;");
        }
        
        keysLayout->addWidget(key);
    }
    
    m_keysScrollArea->setWidget(keysWidget);
    mainLayout->addWidget(m_keysScrollArea);

    // Right Side: Grid + Velocity Lane
    QWidget *rightContainer = new QWidget(this);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(0,0,0,0);
    rightLayout->setSpacing(0);
    
    // Top: Piano Roll Grid
    m_gridScrollArea = new QScrollArea(rightContainer);
    m_gridScrollArea->setWidgetResizable(true);
    m_gridScrollArea->setFrameShape(QFrame::NoFrame);
    m_gridScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // Handled by compose view or velocity
    
    m_grid = new PianoRollGridWidget(m_gridScrollArea);
    m_grid->setScrollArea(m_gridScrollArea);
    m_gridScrollArea->setWidget(m_grid);
    rightLayout->addWidget(m_gridScrollArea, 1);
    
    // Bottom: Velocity Lane
    m_velocityScrollArea = new QScrollArea(rightContainer);
    m_velocityScrollArea->setWidgetResizable(true);
    m_velocityScrollArea->setFrameShape(QFrame::NoFrame);
    m_velocityScrollArea->setFixedHeight(80); // Height + scrollbar
    m_velocityScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    m_velocityLane = new VelocityLaneWidget(m_velocityScrollArea);
    m_velocityScrollArea->setWidget(m_velocityLane);
    rightLayout->addWidget(m_velocityScrollArea, 0);
    
    mainLayout->addWidget(rightContainer, 1);
    
    // テーマ変更時に鍵盤ウィジェットを再スタイル適用
    connect(&Darwin::ThemeManager::instance(), &Darwin::ThemeManager::themeChanged,
            this, &PianoRollView::applyTheme);

    // Sync Vertical Scroll (Keys and Grid)
    connect(m_gridScrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
            m_keysScrollArea->verticalScrollBar(), &QScrollBar::setValue);
            
    // Sync Horizontal Scroll (Grid and Velocity)
    connect(m_velocityScrollArea->horizontalScrollBar(), &QScrollBar::valueChanged,
            m_gridScrollArea->horizontalScrollBar(), &QScrollBar::setValue);
    connect(m_gridScrollArea->horizontalScrollBar(), &QScrollBar::valueChanged,
            m_velocityScrollArea->horizontalScrollBar(), &QScrollBar::setValue);

    // Initial scroll to C3 (roughly)
    // C3 is MIDI note 60. Row index = (127-60) = 67. Row height = 12. 
    // Y position = 67 * 12 = 804
    m_gridScrollArea->verticalScrollBar()->setValue(804 - (m_gridScrollArea->viewport()->height() / 2));
}

QScrollBar* PianoRollView::horizontalScrollBar() const
{
    return m_velocityScrollArea ? m_velocityScrollArea->horizontalScrollBar() : nullptr;
}

void PianoRollView::setProject(Project* project)
{
    m_project = project;
    if (m_grid) {
        m_grid->setProject(project);
    }
}

void PianoRollView::setActiveClip(Clip* clip)
{
    if (m_grid) {
        m_grid->setActiveClip(clip);
    }
    if (m_velocityLane) {
        m_velocityLane->setActiveClip(clip);
    }
    
    // Auto-scroll to notes if clip is valid? (Optional UX improvement for later)
}

// ---------------------------------------------------------------------------
void PianoRollView::applyTheme()
{
    if (!m_keysWidget) return;

    const Darwin::ThemeManager& tm = Darwin::ThemeManager::instance();

    // 鍵盤ウィジェット全体の背景
    {
        const bool isDark2 = tm.isDarkMode();
        const QString kwBg     = isDark2 ? tm.panelBackgroundColor().name() : "#ffffff";
        const QString kwBorder = isDark2 ? tm.borderColor().name() : "#cccccc";
        m_keysWidget->setStyleSheet(
            QString("background-color: %1; border-right: 1px solid %2;").arg(kwBg, kwBorder));
    }

    // 各キーラベルを再スタイル
    const bool isDark = tm.isDarkMode();
    const QString blackBg  = isDark ? tm.pianoBlackKeyColor().name() : "#e2e8f0";
    const QString whiteBg  = isDark ? tm.panelBackgroundColor().name() : "#f8fafc";
    const QString borderCol = isDark ? tm.borderColor().name() : "#f1f5f9";
    const QString monoFontCss = Darwin::FontManager::monoFontCss();
    // 黒鍵・白鍵ともテキストは背景に合わせて固定色
    const QString blackTextCol = isDark ? "#94a3b8" : "#333333";
    const QString whiteTextCol = isDark ? "#475569" : "#333333";
    const QString sharpStyle = QString(
        "background-color: %1; border-bottom: 1px solid %2;"
        " font-family: %4; font-size: 10px; color: %3; font-weight: 500;")
        .arg(blackBg, borderCol, blackTextCol, monoFontCss);
    const QString whiteStyle = QString(
        "background-color: %1; border-bottom: 1px solid %2;"
        " font-family: %4; font-size: 10px; color: %3; font-weight: 500;")
        .arg(whiteBg, borderCol, whiteTextCol, monoFontCss);

    for (QLabel* key : m_keysWidget->findChildren<QLabel*>()) {
        bool isSharp = key->text().contains('#');
        QString style = isSharp ? sharpStyle : whiteStyle;
        if (key->text() == "C3")
            style += "color: #FF3366; font-weight: bold;";
        key->setStyleSheet(style);
    }

    // ベロシティレーンのスクロールエリア背景色を更新（ウィジェット下側の隙間を防ぐ）
    const QString bgStyle = QString("background-color: %1;").arg(tm.backgroundColor().name());
    if (m_velocityScrollArea) {
        m_velocityScrollArea->setStyleSheet(bgStyle);
        m_velocityScrollArea->viewport()->setStyleSheet(bgStyle);
    }
    if (m_keysScrollArea) {
        m_keysScrollArea->setStyleSheet(bgStyle);
        m_keysScrollArea->viewport()->setStyleSheet(bgStyle);
    }
}
