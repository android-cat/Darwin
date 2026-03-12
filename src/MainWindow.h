#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QPushButton>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QUndoStack>
#include <QToolButton>
#include <QPointer>
#include <QtGlobal>

class QAbstractAnimation;
class BulbRaysOverlay;

class Project;
class PlaybackController;
class ComposeView;
class SourceView;
class Track;
class Clip;
class QHBoxLayout;
class RecordModeButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);

private slots:
    void switchMode(int index);
    void onPlayButtonClicked();
    void onRecordButtonClicked();
    void onPlayStateChanged(bool isPlaying);
    void onRecordingStateChanged(bool isRecording);
    void onRecordingFailed(const QString& message);
    void onRecordingCommitted(Track* track, Clip* clip);
    void onPlayheadPositionChanged(qint64 tickPosition);
    void onBpmChanged(double bpm);

    // ファイル操作
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void exportAudio();

private:
    void setupUi();
    void setupHeader();
    void setupTransport();
    void setupMenuBar(QHBoxLayout* parentLayout);
    void setupShortcuts();
    void updateWindowTitle();
    void updateTimecode(qint64 tickPosition);
    void applyGlobalStyle();
    void playBulbAnimation(bool turningOn);
    void clearPendingRecordingUndoState();
    Track* resolveRecordingTarget(bool midiMode, bool* createdTrack = nullptr);

    QStackedWidget *m_stackedWidget;
    SourceView *m_sourceView;
    // ComposeView *m_composeView;
    QPushButton *m_btnSource;
    QPushButton *m_btnCompose;
    QPushButton *m_btnMix;
    
    QPushButton *m_rewindBtn;
    QPushButton *m_skipPrevBtn;
    QPushButton *m_playBtn;
    QPushButton *m_skipNextBtn;
    RecordModeButton *m_recordBtn;
    QDoubleSpinBox *m_bpmSpinBox;
    QLabel *m_timecodeLabel;

    // メニューツールボタン（テーマ変更時にアイコンを更新するために保持）
    QToolButton *m_btnMenu         = nullptr;
    QToolButton *m_btnOpen         = nullptr;
    QToolButton *m_btnSave         = nullptr;
    QToolButton *m_themeToggleBtn  = nullptr;

    // テーマ切り替えアニメーション
    BulbRaysOverlay        *m_raysOverlay        = nullptr;
    QAbstractAnimation     *m_themeAnim          = nullptr;

    // メニューアクション（テーマ変更時にアイコンを更新するために保持）
    QAction *m_newAction    = nullptr;
    QAction *m_openAction   = nullptr;
    QAction *m_saveAction   = nullptr;
    QAction *m_exportAction = nullptr;
    
    Project *m_project;
    PlaybackController *m_playbackController;
    ComposeView *m_composeView;
    QUndoStack *m_undoStack;
    QPointer<Track> m_pendingRecordingTrack;
    bool m_pendingRecordingCreatedTrack = false;
    quint64 m_projectLoadGeneration = 0;
};
