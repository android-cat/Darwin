#pragma once

#include <QObject>
#include <QTimer>
#include <QMutex>
#include <QPointer>
#include <QVector>
#include <atomic>
#include <array>
#include <vector>
#include <cstdint>
#include <unordered_map>

#include "MidiInputDevice.h"

class Project;
class AudioEngine;
class AudioInputCapture;
class Track;
class Clip;

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
    enum class RecordingMode {
        Audio,
        Midi
    };

    explicit PlaybackController(Project* project, QObject* parent = nullptr);
    ~PlaybackController() override;

    // State
    bool isPlaying() const { return m_isPlaying; }
    bool isRecording() const { return m_isRecording.load(); }
    RecordingMode recordingMode() const { return m_recordingMode; }
    Project* project() const { return m_project; }
    AudioEngine* audioEngine() const { return m_audioEngine; }

    // Playback control
    void play();
    void pause();
    void stop();
    void togglePlayPause();
    void seekTo(qint64 tickPosition);
    void setMidiMonitorTrack(Track* track);
    bool startRecording(Track* targetTrack, RecordingMode mode);
    Clip* stopRecording();

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
    void recordingStateChanged(bool isRecording);
    void recordingFailed(const QString& message);
    void recordingCommitted(Track* track, Clip* clip);

private slots:
    void onUiTimerTick();

private:
    /** オーディオコールバック（AudioEngineのレンダリングスレッドから呼ばれる） */
    void audioRenderCallback(float* outputBuffer, int numFrames, int numChannels, double sampleRate);

    /** ロード済みで未準備のプラグインにprepareAudioを呼び出す */
    void ensurePluginsPrepared();
    void ensureTrackPluginsPrepared(Track* track, double sr, int blockSize);
    void updateUiTimerInterval();
    void updateMidiInputState();
    void connectProjectSignals(Project* project);
    void rebuildRoutingCacheLocked();
    void appendRecordedAudio(const float* left, const float* right,
                             int numFrames, double sampleRate);
    void handleMidiMessage(const MidiInputDevice::Message& msg);
    Clip* finalizeAudioRecordingLocked(qint64 recordEndTick);
    Clip* finalizeMidiRecordingLocked(qint64 recordEndTick);

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
    struct LiveMidiMessage {
        int trackId = -1;
        int sampleOffset = 0;
        uint8_t type = 0;       ///< 0=NoteOn,1=NoteOff,2=CC,3=PitchBend,4=ChannelPressure
        int16_t pitch = 0;
        float velocity = 0.0f;
        uint8_t ccNumber = 0;   ///< CC番号 (type==2)
        uint8_t ccValue  = 0;   ///< CC値 (type==2)
        int16_t bendValue = 8192; ///< Pitch Bend値 (type==3)
        uint8_t pressure = 0;   ///< Channel Aftertouch (type==4)
    };

    Project* m_project;
    AudioEngine* m_audioEngine;
    AudioInputCapture* m_audioInputCapture;
    MidiInputDevice* m_midiInputDevice;
    QTimer* m_uiTimer; // UI更新用タイマー

    // 再生状態（オーディオスレッドからもアクセス）
    std::atomic<bool> m_isPlaying{false};
    std::atomic<double> m_playPositionTicks{0.0}; // 高精度再生位置（ティック）
    std::atomic<bool> m_isRecording{false};
    RecordingMode m_recordingMode = RecordingMode::Audio;
    QPointer<Track> m_recordTrack;
    qint64 m_recordStartTick = 0;
    QMutex m_recordMutex;
    QVector<float> m_recordedAudioL;
    QVector<float> m_recordedAudioR;
    double m_recordedAudioSampleRate = 0.0;

    struct RecordedMidiNote {
        int pitch = 0;
        qint64 startTick = 0;
        qint64 durationTicks = 1;
        int velocity = 0;
    };
    struct ActiveRecordedMidiNote {
        int pitch = 0;
        qint64 startTick = 0;
        int velocity = 0;
    };
    struct RecordedCCEvent {
        int ccNumber = 0;     ///< CC番号 (128=PitchBend, 129=ChannelPressure)
        qint64 tick = 0;      ///< クリップ先頭からの相対ティック
        int value = 0;        ///< 値
    };
    QVector<RecordedMidiNote> m_recordedMidiNotes;
    QVector<ActiveRecordedMidiNote> m_activeRecordedMidiNotes;
    QVector<RecordedCCEvent> m_recordedCCEvents;

    QPointer<Track> m_midiMonitorTrack;
    QMutex m_liveMidiMutex;
    QVector<LiveMidiMessage> m_liveMidiMessages;
    std::array<int, 128> m_liveHeldNoteCounts{};
    int m_midiMonitorTrackId = -1;

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
