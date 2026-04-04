#pragma once

#include <QWidget>
#include <QTimer>
#include <QPixmap>
#include <QScrollArea>

// 前方宣言
class VST3PluginInstance;
class QLabel;

namespace Steinberg {
class IPlugView;
class IPlugFrame;
}

/**
 * @brief ビットマップキャプチャモード用のスケーリング表示ウィジェット
 *
 * プラグインGUIのキャプチャ画像をスケーリングして描画し、
 * マウスイベントをネイティブ座標に変換してプラグインHWNDへ転送する
 */
class ScaledPluginDisplay : public QWidget
{
    Q_OBJECT

public:
    explicit ScaledPluginDisplay(QWidget* parent = nullptr);

    /** スケーリング済みのキャプチャ画像をセット */
    void setPixmap(const QPixmap& pixmap);
    /** 転送先のプラグインHWNDを設定 */
    void setPluginHwnd(void* hwnd);
    /** スケール倍率を設定 */
    void setScaleFactor(double factor);
    /** プラグインのネイティブサイズを設定 */
    void setNativeSize(int w, int h);

signals:
    /** マウス操作があった（即時キャプチャ更新のトリガー用） */
    void userInteracted();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    /** マウスイベントをプラグインHWNDに転送（座標変換付き） */
    void forwardMouseEvent(QMouseEvent* event, unsigned int msg);
    /** 表示座標→プラグインネイティブ座標に変換 */
    QPoint scaleToNative(const QPoint& pos) const;

    QPixmap m_pixmap;           ///< 表示用スケーリング済み画像
    void* m_pluginHwnd;         ///< プラグインのコンテナHWND
    double m_scaleFactor;       ///< 現在のスケール倍率
    int m_nativeWidth;          ///< プラグインの元幅
    int m_nativeHeight;         ///< プラグインの元高さ
};

/**
 * @brief VST3プラグインのGUIをQt内に埋め込み表示するウィジェット
 *
 * canResize対応プラグイン: ウィジェットサイズに合わせてプラグインをリサイズ。
 * canResize非対応プラグイン: ネイティブサイズをスクロールエリアに表示。
 *   ウィジェットが小さい場合はスクロールバーで全体を閲覧できる。
 */
class PluginEditorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PluginEditorWidget(QWidget* parent = nullptr);
    ~PluginEditorWidget() override;

    /** プラグインのGUIエディターを開く */
    bool openEditor(VST3PluginInstance* instance);
    /** エディターを閉じてリソースを解放 */
    void closeEditor();
    /** エディターが開いているか */
    bool isEditorOpen() const { return m_plugView != nullptr; }
    /** プラグインからのリサイズ要求を処理 */
    void updateContainerSize(int w, int h);

public slots:
    /** キャプチャ更新 */
    void capturePluginView();

protected:
    /** ウィジェットのリサイズに応じてスケーリングを自動更新 */
    void resizeEvent(QResizeEvent* event) override;
    /** 親UIへのイベント伝播を必要に応じて打ち止めにする */
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /** 監視対象のレイアウト変化がサイズ同期を促すべきか判定する */
    bool shouldScheduleViewSync(QObject* watched, QEvent* event) const;
    /** 現在のウィジェットサイズに応じてスケールモードを更新 */
    void updateScaleMode();
    /** レイアウト確定後にプラグインビューのサイズ同期をやり直す */
    void scheduleDeferredViewSync();
    /** プラグインビュー優先の入力設定を適用/解除する */
    void setPluginInputPriority(bool active);
    /** 親UIへ渡したくない入力イベントか判定する */
    bool shouldConsumePriorityEvent(QObject* watched, QEvent* event) const;
    /** ダイレクトモードに切り替え（プラグインHWNDを直接表示） */
    void enterDirectMode();
    /** ビットマップキャプチャモードに切り替え */
    void enterBitmapCaptureMode();

    Steinberg::IPlugView* m_plugView;      ///< プラグインのビューインターフェース
    Steinberg::IPlugFrame* m_plugFrame;    ///< プラグインフレーム

    // プラグイン表示用
    QWidget* m_container;                  ///< プラグインHWNDのホストコンテナ
    QScrollArea* m_scrollArea;             ///< プラグインコンテナのスクロールエリア
    QLabel* m_scaleLabel;                  ///< スケール率表示ラベル

    // ビットマップキャプチャモード用
    QWidget* m_offscreenHost;              ///< オフスクリーンのトップレベルウィンドウ
    ScaledPluginDisplay* m_scaledDisplay;  ///< スケーリング表示ウィジェット
    QTimer m_captureTimer;                 ///< 定期キャプチャタイマー

    // スケーリング状態
    double m_scaleFactor;                  ///< 現在のスケール倍率（1.0 = 100%）
    int m_nativeWidth;                     ///< プラグインの元幅
    int m_nativeHeight;                    ///< プラグインの元高さ
    bool m_supportsResize;                 ///< canResize()対応プラグインか
    bool m_bitmapCaptureActive;            ///< ビットマップキャプチャモードか
    bool m_deferredViewSyncPending;        ///< 遅延同期が予約済みか
};
