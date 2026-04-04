#pragma once
#include <QWidget>
#include <QTimer>
#include <functional>

struct VST3PluginInfo;

class Track;
class LevelMeterWidget;
class VST3Scanner;
class QVBoxLayout;
class VST3PluginInstance;
class QPushButton;
class QGraphicsOpacityEffect;

class MixerChannelWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MixerChannelWidget(int trackNumber, const QString &trackName, Track* track = nullptr, QWidget *parent = nullptr);

    // Master向け
    void setLevel(float left, float right);

public slots:
    void updateFxSlots();
    void applyTheme();

private slots:
    void showFxPluginMenu();

signals:
    void pluginEditorRequested(VST3PluginInstance* plugin);
    void folderToggleRequested(Track* track);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void animateFxBurst(QPushButton* btn);
    void animateFxAppear(int fxIndex, int retryCount = 0);
    void showFxPluginMenuAfterScan(
        std::function<void(QVector<VST3PluginInfo>&)> loadUsageData,
        std::function<void(const QString&)> saveUsageData);
    
    Track* m_track;
    LevelMeterWidget* m_levelMeterL;
    LevelMeterWidget* m_levelMeterR;
    
    QWidget* m_fxContainer;
    QVBoxLayout* m_fxLayout;
    VST3Scanner* m_scanner;
    
    int m_pendingFxHighlight = -1;

    // Drag & Drop UI state
    QWidget* m_dragGhost = nullptr;
    QWidget* m_dropIndicator = nullptr;
    QPoint m_dragOffset;
    bool m_isDraggingFx = false;
    VST3PluginInstance* m_dragFxPlugin = nullptr;
    int m_dragInsertIndex = -1;
    void beginFxDrag(QPushButton* btn, int index);
    void finishFxDrag();
};
