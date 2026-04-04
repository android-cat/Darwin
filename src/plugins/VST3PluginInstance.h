#pragma once

#include <QString>
#include <QObject>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

// VST3 SDK 前方宣言
namespace VST3 {
namespace Hosting {
class Module;
class PluginFactory;
class ClassInfo;
}
}

namespace Steinberg {
class IPlugView;
namespace Vst {
class IComponent;
class IEditController;
class IAudioProcessor;
class PlugProvider;
}
}

/**
 * @brief VST3プラグインの単一インスタンスを管理するクラス
 * DLLロード → コンポーネント初期化 → GUI取得を一括管理
 */
class VST3PluginInstance : public QObject
{
    Q_OBJECT

public:
    explicit VST3PluginInstance(QObject* parent = nullptr);
    ~VST3PluginInstance() override;

    /**
     * @brief VST3プラグインをロード
     * @param path .vst3ファイルまたはバンドルのパス
     * @return 成功時true
     */
    bool load(const QString& path);

    /**
     * @brief プラグインをアンロードし、全リソースを解放
     */
    void unload();

    /**
     * @brief プラグインがロード済みかどうか
     */
    bool isLoaded() const { return m_loaded; }

    /**
     * @brief プラグインのエディタービュー（GUI）を取得
     * @return IPlugView*（null可能）。呼び出し側で管理。
     */
    Steinberg::IPlugView* createView();

    /**
     * @brief プラグイン名を取得
     */
    QString pluginName() const { return m_pluginName; }

    /**
     * @brief プラグインパスを取得
     */
    QString pluginPath() const { return m_pluginPath; }

    // ===== オーディオ処理 =====

    /**
     * @brief オーディオプロセッシング準備（サンプルレート・バッファサイズ設定）
     * @return 成功時true
     */
    bool prepareAudio(double sampleRate, int maxBlockSize);

    /**
     * @brief MIDIイベント（NoteOn/NoteOff/CC/PitchBend）
     */
    struct MidiEvent {
        int32_t sampleOffset; ///< バッファ内のサンプルオフセット
        uint8_t type;         ///< 0=NoteOn, 1=NoteOff, 2=CC, 3=PitchBend
        int16_t pitch;        ///< MIDIノートナンバー (0-127) ※NoteOn/Off用
        float velocity;       ///< ベロシティ (0.0-1.0) ※NoteOn/Off用
        uint8_t ccNumber = 0; ///< CC番号 (0-127) ※type==2用
        uint8_t ccValue  = 0; ///< CC値 (0-127) ※type==2用
        int16_t bendValue = 8192; ///< Pitch Bend値 (0-16383, 中央=8192) ※type==3用
    };

    /**
     * @brief プラグインに渡すトランスポート情報
     */
    struct TransportInfo {
        double positionInTicks = 0.0;   ///< 現在位置（ティック）
        double bpm = 120.0;             ///< テンポ (BPM)
        double sampleRate = 44100.0;    ///< サンプルレート
        bool isPlaying = false;         ///< 再生中か
        int timeSigNumerator = 4;       ///< 拍子の分子
        int timeSigDenominator = 4;     ///< 拍子の分母
        int ticksPerBeat = 480;         ///< 1拍あたりのティック数
    };

    /**
     * @brief オーディオバッファを処理
     * @param inputLeft   左チャンネル入力（読み取り、null可）
     * @param inputRight  右チャンネル入力（読み取り、null可）
     * @param outputLeft  左チャンネル出力（書き込み先、numFrames個）
     * @param outputRight 右チャンネル出力（書き込み先、numFrames個）
     * @param numFrames   フレーム数
     * @param midiEvents  MIDIイベント配列
     * @param transport   トランスポート情報（ProcessContextとしてプラグインに渡される）
     */
    void processAudio(float* inputLeft, float* inputRight,
                      float* outputLeft, float* outputRight, int numFrames,
                      const std::vector<MidiEvent>& midiEvents,
                      const TransportInfo& transport);

    /**
     * @brief プラグインからのリスタート要求フラグを通知（ComponentHandlerから呼ばれる）
     */
    void signalRestart(int flags);

    /**
     * @brief 保留中のリスタートフラグを取得しクリアする
     */
    int consumeRestartFlags();

    /** オーディオ準備済みか */
    bool isAudioPrepared() const { return m_audioPrepared; }

    /** 現在のサンプルレート */
    double currentSampleRate() const { return m_currentSampleRate; }

    /** バイパス状態の取得と設定 */
    bool isBypassed() const { return m_isBypassed; }
    void setBypassed(bool bypassed) { m_isBypassed = bypassed; }

    // ===== 状態保存/復元 =====

    /**
     * @brief プラグインのコンポーネント状態を取得（プロジェクト保存用）
     * @return 状態データ（バイナリ）。失敗時は空のQByteArray
     */
    QByteArray getComponentState() const;

    /**
     * @brief プラグインのコントローラー状態を取得（プロジェクト保存用）
     * @return 状態データ（バイナリ）。失敗時は空のQByteArray
     */
    QByteArray getControllerState() const;

    /**
     * @brief プラグインのコンポーネント状態を復元（プロジェクト読み込み用）
     * @param data getComponentState()で取得した状態データ
     * @return 成功時true
     */
    bool setComponentState(const QByteArray& data);

    /**
     * @brief プラグインのコントローラー状態を復元（プロジェクト読み込み用）
     * @param data getControllerState()で取得した状態データ
     * @return 成功時true
     */
    bool setControllerState(const QByteArray& data);

private:
    bool m_loaded;
    QString m_pluginName;
    QString m_pluginPath;
    bool m_isBypassed = false;

    // VST3 SDK オブジェクト
    std::shared_ptr<VST3::Hosting::Module> m_module;
    Steinberg::Vst::PlugProvider* m_plugProvider;

    // オーディオ処理
    bool m_audioPrepared = false;
    double m_currentSampleRate = 44100.0;
    int m_maxBlockSize = 1024;
    int m_numAudioInputBuses = 0;   ///< オーディオ入力バス数（純粋なインストゥルメントは0）
    int m_numAudioOutputBuses = 0;  ///< オーディオ出力バス数
    std::vector<float> m_bufferLeft;
    std::vector<float> m_bufferRight;

    // プラグインからのリスタート要求フラグ（スレッドセーフ）
    std::atomic<int> m_pendingRestartFlags{0};

    // 連続再生サンプル数の追跡（ProcessContext用）
    int64_t m_continousTimeSamples = 0;
};
