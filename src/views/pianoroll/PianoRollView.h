#pragma once
#include <QWidget>

class PianoRollGridWidget;
class VelocityLaneWidget;
class ExpressionLaneWidget;
class QScrollArea;
class QScrollBar;
class QButtonGroup;
class Project;
class Clip;

class PianoRollView : public QWidget
{
    Q_OBJECT
public:
    PianoRollView(QWidget *parent = nullptr);
    
    PianoRollGridWidget* gridWidget() const { return m_grid; }
    VelocityLaneWidget* velocityWidget() const { return m_velocityLane; }
    ExpressionLaneWidget* expressionWidget() const { return m_expressionLane; }
    
    QScrollBar* horizontalScrollBar() const;
    
    void setProject(Project* project);
    void setActiveClip(Clip* clip);

    /** レーンエリアの現在の高さ */
    int laneHeight() const { return m_laneHeight; }
    /** レーンエリアの高さを設定（0でチップバーのみ表示） */
    void setLaneHeight(int height);

private slots:
    void applyTheme();
    void onLaneChipClicked(int id);

private:
    void buildLaneChips(QWidget* container);

    Project* m_project;
    PianoRollGridWidget* m_grid;
    VelocityLaneWidget* m_velocityLane;
    ExpressionLaneWidget* m_expressionLane;
    
    QScrollArea* m_keysScrollArea;
    QScrollArea* m_gridScrollArea;
    QScrollArea* m_velocityScrollArea;
    QScrollArea* m_expressionScrollArea;
    QWidget* m_keysWidget = nullptr;
    QButtonGroup* m_laneChipGroup = nullptr;
    QWidget* m_laneResizeHandle = nullptr;
    int m_laneHeight = 80;  ///< レーンの高さ（0=折りたたみ, 最大200）
};
