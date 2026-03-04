#pragma once

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QPointer>
#include "VST3Scanner.h"

class ArrangementGridWidget;
class PianoRollGridWidget;
class ArrangementView;
class PluginEditorWidget;
class QStackedWidget;
class QLabel;
class QListWidgetItem;
class Track;

/**
 * @brief コンポーズビュー
 * アレンジメント・ピアノロール・プラグインエディタを統合したメインビュー。
 * 未設定トラックが選択された場合はプラグイン選択パネルを表示し、
 * 設定済みトラックの場合は削除ボタン付きエディタを表示する。
 */
class ComposeView : public QWidget
{
    Q_OBJECT
public:
    explicit ComposeView(QWidget *parent = nullptr);

    void setProject(class Project* project);
    void setUndoStack(class QUndoStack* stack);

    ArrangementView* arrangementView() const { return m_arrangementView; }
    ArrangementGridWidget* arrangementGrid() const { return m_arrangementGrid; }
    PianoRollGridWidget* pianoRollGrid() const { return m_pianoRollGrid; }
    class PianoRollView* pianoRollView() const { return m_pianoRollView; }

private slots:
    /** トラック選択時のハンドラ */
    void onTrackSelected(Track* track);
    /** プラグインリストのアイテムクリック */
    void onPluginItemClicked(QListWidgetItem* item);
    /** 選択したプラグインをトラックに紐づける */
    void onLoadPluginClicked();
    /** トラックとプラグインの紐づけを解除する */
    void onDetachPluginClicked();
    /** VST3プラグインを再スキャンする */
    void onRescanClicked();
    /** テーマ変更時にスタイルを再適用 */
    void applyTheme();

private:
    void setupUi();
    /** スキャン済みリストをプラグインリストウィジェットに反映 */
    void refreshPluginList();
    /** 現在選択中トラックのプラグインエディタを開く */
    void openEditorForCurrentTrack();

    ArrangementView* m_arrangementView;
    ArrangementGridWidget* m_arrangementGrid;
    PianoRollGridWidget* m_pianoRollGrid;
    class PianoRollView* m_pianoRollView;

    /** 右パネル（3ページ構成のスタックウィジェット） */
    QStackedWidget* m_editorStack;

    // ページ0: トラック未選択
    QWidget* m_noPluginPanel;
    QLabel* m_noPluginLabel;

    // ページ1: プラグイン未設定トラック選択時
    QWidget* m_pluginSelectorPanel;
    QLabel* m_selectorTrackLabel;
    QListWidget* m_pluginListWidget;
    QPushButton* m_loadPluginBtn;
    QPushButton* m_rescanBtn;
    QLabel* m_scanStatusLabel;

    // ページ2: プラグイン設定済みトラック選択時
    QWidget* m_editorContainer;
    QLabel* m_editorTrackLabel;
    QPushButton* m_detachBtn;
    PluginEditorWidget* m_pluginEditor;

    // スプリッター参照（テーマ変更時にスタイル更新）
    QSplitter* m_mainSplitter   = nullptr;
    QSplitter* m_vSplitter      = nullptr;
    QWidget*   m_editorHeaderBar = nullptr;

    // スキャナ & 状態管理
    VST3Scanner* m_scanner;
    QVector<VST3PluginInfo> m_scannedPlugins;   ///< スキャン済みプラグイン一覧
    int m_selectedPluginIndex;                   ///< 選択中プラグインのインデックス
    QPointer<Track> m_currentTrack;              ///< 現在選択中のトラック
};
