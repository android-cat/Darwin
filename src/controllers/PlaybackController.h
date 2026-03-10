#pragma once

#include <QObject>
#include <QTimer>
#include <QMutex>
#include <atomic>
#include <array>
#include <vector>
#include <cstdint>
#include <unordered_map>

class Project;
class AudioEngine;
class Track;

/**
 * @brief 再生制御を管理するコントローラー
 *
 * AudioEngineと連携し、VST3プラグインへの
 * MIDIイベント送信とオーディオ出力を制御する。
 */
class PlaybackController : public QObject
{
    Q_OBJECT

public:
    explicit PlaybackController(Project* project, QObject* parent = nullptr);
    ~PlaybackController() override;

    // State
    bool isPlaying() const { return m_isPlaying; }
    Project* project() const { return m_project; }
    AudioEngine* audioEngine() const { return m_audioEngine; }

    // Playback control
    void play();
    void pause();
    void stop();
    void togglePlayPause();
    void seekTo(qint64 tickPosition);

    /** エクスポート用: AudioEngine停止・タイマー停止してプラグインを専有させる */
    void suspendForExport();
    /** エクスポート用: AudioEngine・タイマーを再開してプラグインを元のサンプルレートに戻す */
    void resumeFromExport();

public slots:
    void setProject(Project* project);

signals:
    void playStateChanged(bool isPlaying);
    void positionChanged(qint64 tickPosition);
    void masterLevelChanged(float levelL, float levelR);
    void trackLevelChanged(int trackIndex, float levelL, float levelR);

private slots:
    void onUiTimerTick();

private:
    /** オーディオコールバック（AudioEngineのレンダリングスレッドから呼ばれる） */
    void audioRenderCallback(float* outputBuffer, int numFrames, int numChannels, double sampleRate);

    /** ロード済みで未準備のプラグインにprepareAudioを呼び出す */
    void ensurePluginsPrepared();
    void ensureTrackPluginsPrepared(Track* track, double sr, int blockSize);
    void updateUiTimerInterval();
    void connectProjectSignals(Project* project);
    void rebuildRoutingCacheLocked();

    /** ティック範囲内のMIDIイベントを収集 */
    struct MidiEventInternal {
        int sampleOffset;
        uint8_t type; // 0=NoteOn, 1=NoteOff
        int16_t pitch;
        float velocity;
        int trackIndex;
    };
    void collectMidiEvents(double startTick, double endTick, int numFrames,
                           double ticksPerSample, std::vector<MidiEventInternal>& events);

    /** アクティブノートのNoteOff処理 */
    struct ActiveNote {
        int trackIndex;
        int16_t pitch;
        double endTick; // グローバルティック
    };

    Project* m_project;
    AudioEngine* m_audioEngine;
    QTimer* m_uiTimer; // UI更新用タイマー

    // 再生状態（オーディオスレッドからもアクセス）
    std::atomic<bool> m_isPlaying{false};
    std::atomic<double> m_playPositionTicks{0.0}; // 高精度再生位置（ティック）

    // アクティブノート追跡（オーディオスレッド専用）
    std::vector<ActiveNote> m_activeNotes;

    // レベルメーター用（オーディオスレッド -> UIスレッド通信用）
    std::atomic<float> m_peakL{0.0f};
    std::atomic<float> m_peakR{0.0f};
    
    // トラック毎のピーク値（インデックスアクセス）
    // UI側の更新用に適当なサイズを確保しておく
    static constexpr size_t MAX_TRACKS_METERING = 128;
    static constexpr int UI_TIMER_INTERVAL_PLAYING_MS = 16;
    static constexpr int UI_TIMER_INTERVAL_IDLE_MS = 50;
    static constexpr int IDLE_MAINTENANCE_INTERVAL_TICKS = 4;
    std::array<std::atomic<float>, MAX_TRACKS_METERING> m_trackPeakL;
    std::array<std::atomic<float>, MAX_TRACKS_METERING> m_trackPeakR;

    // ミキシング用一時バッファ
    std::vector<float> m_mixBufL;
    std::vector<float> m_mixBufR;
    std::vector<float> m_trackBufL;
    std::vector<float> m_trackBufR;
    // オーディオコールバック内の動的確保を避けるため、プラグイン用バッファも再利用する。
    std::vector<float> m_pluginBufL;
    std::vector<float> m_pluginBufR;

    // フォルダバス（フォルダトラックID → L/Rバッファ）
    struct FolderBus {
        std::vector<float> bufL;
        std::vector<float> bufR;
    };
    std::unordered_map<int, FolderBus> m_folderBuses;
    // フォルダの深さ順ルーティングはトラック構造変更時だけ再構築する。
    std::unordered_map<int, Track*> m_trackByIdCache;
    std::vector<int> m_sortedFolderTrackIndices;
    std::atomic<bool> m_routingCacheDirty{true};
    int m_idleMaintenanceTick = 0;
};
