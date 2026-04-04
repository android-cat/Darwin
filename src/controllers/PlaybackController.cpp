#include "PlaybackController.h"
#include "AudioEngine.h"
#include "AudioInputCapture.h"
#include "MidiInputDevice.h"
#include "Project.h"
#include "Track.h"
#include "Clip.h"
#include "Note.h"
#include "CCEvent.h"
#include "VST3PluginInstance.h"
#include "WavWriter.h"
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <cstring>
#include <unordered_set>
#include <utility>
#include "common/ModelAccessLock.h"

namespace {
QString makeRecordingFilePath()
{
    const QString baseDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/Recordings");
    QDir().mkpath(baseDir);

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString suffix = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    return baseDir + QStringLiteral("/take_%1_%2.wav").arg(stamp, suffix);
}
}

PlaybackController::PlaybackController(Project* project, QObject* parent)
    : QObject(parent)
    , m_project(project)
    , m_audioEngine(new AudioEngine(this))
    , m_audioInputCapture(new AudioInputCapture(this))
    , m_midiInputDevice(new MidiInputDevice(this))
    , m_uiTimer(new QTimer(this))
{
    // AudioEngineの初期化
    if (!m_audioEngine->initialize()) {
        qWarning() << "PlaybackController: AudioEngineの初期化に失敗";
    }

    // AudioEngineのレンダリングコールバックを設定
    m_audioEngine->setRenderCallback(
        [this](float* output, int numFrames, int numChannels, double sampleRate) {
            audioRenderCallback(output, numFrames, numChannels, sampleRate);
        });

    // AudioEngineを常時起動（停止中もプラグインGUIプレビューの音を処理するため）
    if (!m_audioEngine->isRunning()) {
        m_audioEngine->start();
    }

    // 停止中は少し間引き、再生中だけ高頻度で更新する。
    m_uiTimer->setInterval(UI_TIMER_INTERVAL_IDLE_MS);
    connect(m_uiTimer, &QTimer::timeout, this, &PlaybackController::onUiTimerTick);
    m_uiTimer->start(); // 常時稼働

    m_audioInputCapture->setCaptureCallback(
        [this](const float* left, const float* right, int numFrames, double sampleRate) {
            appendRecordedAudio(left, right, numFrames, sampleRate);
        });
    m_midiInputDevice->setMessageCallback(
        [this](const MidiInputDevice::Message& message) {
            handleMidiMessage(message);
        });

    connectProjectSignals(m_project);
}

PlaybackController::~PlaybackController()
{
    setMidiMonitorTrack(nullptr);
    stopRecording();
    stop();
    m_uiTimer->stop();
    m_audioEngine->stop();
}

void PlaybackController::setProject(Project* project)
{
    if (m_project != project) {
        if (m_project) {
            disconnect(m_project, nullptr, this, nullptr);
        }
        setMidiMonitorTrack(nullptr);
        stopRecording();
        stop();
        m_project = project;
        connectProjectSignals(m_project);
        m_routingCacheDirty.store(true);
    }
}

void PlaybackController::connectProjectSignals(Project* project)
{
    if (!project) {
        return;
    }

    auto invalidateRouting = [this]() {
        m_routingCacheDirty.store(true);
    };

    connect(project, &Project::trackAdded, this, invalidateRouting);
    connect(project, &Project::trackRemoved, this, invalidateRouting);
    connect(project, &Project::trackOrderChanged, this, invalidateRouting);
    connect(project, &Project::folderStructureChanged, this, invalidateRouting);
}

void PlaybackController::rebuildRoutingCacheLocked()
{
    // フォルダの深さ順とID索引を先に作り、オーディオコールバックでは再利用する。
    m_trackByIdCache.clear();
    m_sortedFolderTrackIndices.clear();

    if (!m_project) {
        m_folderBuses.clear();
        return;
    }

    const auto& tracks = m_project->tracks();
    std::unordered_set<int> activeFolderIds;
    activeFolderIds.reserve(static_cast<size_t>(tracks.size()));

    for (int ti = 0; ti < tracks.size(); ++ti) {
        Track* track = tracks[ti];
        if (!track) {
            continue;
        }

        m_trackByIdCache[track->id()] = track;
        if (track->isFolder()) {
            m_sortedFolderTrackIndices.push_back(ti);
            activeFolderIds.insert(track->id());
        }
    }

    auto depthForTrack = [this](Track* track) {
        int depth = 0;
        for (int folderId = track ? track->parentFolderId() : -1; folderId >= 0; ) {
            auto it = m_trackByIdCache.find(folderId);
            if (it == m_trackByIdCache.end() || !it->second) {
                break;
            }
            ++depth;
            folderId = it->second->parentFolderId();
        }
        return depth;
    };

    std::sort(m_sortedFolderTrackIndices.begin(), m_sortedFolderTrackIndices.end(),
              [&tracks, &depthForTrack](int lhs, int rhs) {
                  return depthForTrack(tracks[lhs]) > depthForTrack(tracks[rhs]);
              });

    for (auto it = m_folderBuses.begin(); it != m_folderBuses.end(); ) {
        if (activeFolderIds.find(it->first) == activeFolderIds.end()) {
            it = m_folderBuses.erase(it);
        } else {
            ++it;
        }
    }
}

void PlaybackController::play()
{
    if (!m_project || m_isPlaying.load()) {
        return;
    }

    // 全トラックのプラグインにオーディオ準備を通知
    ensurePluginsPrepared();

    // バッファ確保
    int blockSize = m_audioEngine->bufferSize();
    if (blockSize <= 0) blockSize = 1024;
    m_mixBufL.resize(blockSize, 0.0f);
    m_mixBufR.resize(blockSize, 0.0f);
    m_trackBufL.resize(blockSize, 0.0f);
    m_trackBufR.resize(blockSize, 0.0f);

    // トラック数分のピークバッファ確保 (MAX 128 tracks)
    int numTracks = std::min(static_cast<int>(m_project->tracks().size()), static_cast<int>(MAX_TRACKS_METERING));
    for (int i = 0; i < numTracks; ++i) {
        m_trackPeakL[i].store(0.0f);
        m_trackPeakR[i].store(0.0f);
    }
    
    // 再生位置をプロジェクトの現在位置から開始
    m_playPositionTicks.store(static_cast<double>(m_project->playheadPosition()));
    m_activeNotes.clear();
    
    m_isPlaying.store(true);
    m_idleMaintenanceTick = 0;
    updateUiTimerInterval();

    // AudioEngineが何らかの理由で停止している場合は再開始
    if (!m_audioEngine->isRunning()) {
        if (!m_audioEngine->start()) {
            qWarning() << "PlaybackController: AudioEngineの開始に失敗";
            m_isPlaying.store(false);
            return;
        }
    }

    qDebug() << "Playback started. BPM:" << m_project->bpm()
             << "SampleRate:" << m_audioEngine->sampleRate()
             << "BlockSize:" << blockSize;

    emit playStateChanged(true);
}

void PlaybackController::pause()
{
    if (!m_isPlaying.load()) {
        return;
    }

    m_isPlaying.store(false);
    // AudioEngineは停止しない（プラグインGUIプレビューの音を処理し続けるため）
    m_activeNotes.clear();
    m_idleMaintenanceTick = 0;
    updateUiTimerInterval();

    emit playStateChanged(false);
}

void PlaybackController::stop()
{
    bool wasPlaying = m_isPlaying.load();

    m_isPlaying.store(false);
    // AudioEngineは停止しない（プラグインGUIプレビューの音を処理し続けるため）
    m_activeNotes.clear();
    m_idleMaintenanceTick = 0;
    updateUiTimerInterval();

    if (m_project) {
        m_playPositionTicks.store(0.0);
        m_project->setPlayheadPosition(0);
        emit positionChanged(0);
    }

    if (wasPlaying) {
        emit playStateChanged(false);
    }
}

void PlaybackController::togglePlayPause()
{
    if (m_isPlaying.load()) {
        pause();
    } else {
        play();
    }
}

void PlaybackController::seekTo(qint64 tickPosition)
{
    m_playPositionTicks.store(static_cast<double>(tickPosition));
    if (m_project) {
        m_project->setPlayheadPosition(tickPosition);
        emit positionChanged(tickPosition);
    }
    // シーク時にアクティブノートをクリア
    m_activeNotes.clear();
}

void PlaybackController::setMidiMonitorTrack(Track* track)
{
    Track* nextTrack = (track && !track->isFolder()) ? track : nullptr;
    const int nextTrackId = nextTrack ? nextTrack->id() : -1;

    bool changed = false;
    {
        QMutexLocker locker(&m_liveMidiMutex);
        if (m_midiMonitorTrackId != nextTrackId) {
            if (m_midiMonitorTrackId >= 0) {
                // モニター先を切り替える時は、旧トラックで押しっぱなしになっている
                // ノートを必ず解放してから新しいトラックへ切り替える。
                for (int pitch = 0; pitch < static_cast<int>(m_liveHeldNoteCounts.size()); ++pitch) {
                    if (m_liveHeldNoteCounts[pitch] <= 0) {
                        continue;
                    }
                    LiveMidiMessage noteOff;
                    noteOff.trackId = m_midiMonitorTrackId;
                    noteOff.type = 1;
                    noteOff.pitch = static_cast<int16_t>(pitch);
                    noteOff.velocity = 0.0f;
                    m_liveMidiMessages.push_back(noteOff);
                }
            }
            m_liveHeldNoteCounts.fill(0);
            m_midiMonitorTrackId = nextTrackId;
            changed = true;
        }
    }

    m_midiMonitorTrack = nextTrack;

    if (nextTrack && m_audioEngine) {
        // 停止中のライブモニターでもすぐ音が出せるよう、
        // 選択された瞬間に対象プラグインだけ先に prepare しておく。
        QMutexLocker<QRecursiveMutex> modelLock(&Darwin::modelAccessMutex());
        const double sampleRate =
            m_audioEngine->sampleRate() > 0.0 ? m_audioEngine->sampleRate() : 44100.0;
        int blockSize = m_audioEngine->bufferSize();
        if (blockSize <= 0) {
            blockSize = 1024;
        }
        ensureTrackPluginsPrepared(nextTrack, sampleRate, blockSize);
    }

    if (changed) {
        updateMidiInputState();
    }
}

bool PlaybackController::startRecording(Track* targetTrack, RecordingMode mode)
{
    if (!m_project || !targetTrack) {
        emit recordingFailed(QStringLiteral("録音先トラックが見つかりません。"));
        return false;
    }

    if (targetTrack->isFolder()) {
        emit recordingFailed(QStringLiteral("フォルダトラックには録音できません。"));
        return false;
    }

    if (m_isRecording.load()) {
        return true;
    }

    {
        QMutexLocker locker(&m_recordMutex);
        m_recordedAudioL.clear();
        m_recordedAudioR.clear();
        m_recordedAudioSampleRate = 0.0;
        m_recordedMidiNotes.clear();
        m_activeRecordedMidiNotes.clear();
    }

    m_recordTrack = targetTrack;
    m_recordStartTick = m_project->playheadPosition();
    m_recordingMode = mode;

    bool inputReady = false;
    if (mode == RecordingMode::Audio) {
        inputReady = m_audioInputCapture->start();
        if (!inputReady) {
            emit recordingFailed(QStringLiteral("マイク入力を開始できませんでした。"));
            m_recordTrack.clear();
            return false;
        }
    } else {
        inputReady = m_midiInputDevice->start();
        if (!inputReady) {
            emit recordingFailed(QStringLiteral("MIDI入力デバイスを開始できませんでした。"));
            m_recordTrack.clear();
            return false;
        }
    }

    m_isRecording.store(true);

    // 録音は常に現在のプレイヘッド位置から開始し、その長さぶんのクリップを後で確定する。
    if (!m_isPlaying.load()) {
        play();
    }

    if (!m_isPlaying.load()) {
        m_isRecording.store(false);
        if (mode == RecordingMode::Audio) {
            m_audioInputCapture->stop();
        } else {
            updateMidiInputState();
        }
        m_recordTrack.clear();
        emit recordingFailed(QStringLiteral("再生エンジンを開始できませんでした。"));
        return false;
    }

    emit recordingStateChanged(true);
    return true;
}

Clip* PlaybackController::stopRecording()
{
    if (!m_isRecording.load()) {
        return nullptr;
    }

    const RecordingMode mode = m_recordingMode;
    m_isRecording.store(false);

    if (mode == RecordingMode::Audio) {
        m_audioInputCapture->stop();
    } else {
        updateMidiInputState();
    }

    const qint64 recordEndTick =
        std::max<qint64>(m_recordStartTick + 1,
                         static_cast<qint64>(m_playPositionTicks.load()));

    Clip* clip = nullptr;
    {
        QMutexLocker<QRecursiveMutex> modelLock(&Darwin::modelAccessMutex());
        if (mode == RecordingMode::Audio) {
            clip = finalizeAudioRecordingLocked(recordEndTick);
        } else {
            clip = finalizeMidiRecordingLocked(recordEndTick);
        }
    }

    pause();
    emit recordingStateChanged(false);
    if (clip && m_recordTrack) {
        emit recordingCommitted(m_recordTrack, clip);
    }
    m_recordTrack.clear();
    return clip;
}

void PlaybackController::appendRecordedAudio(const float* left, const float* right,
                                             int numFrames, double sampleRate)
{
    if (!m_isRecording.load() || m_recordingMode != RecordingMode::Audio ||
        numFrames <= 0 || !left || !right) {
        return;
    }

    QMutexLocker locker(&m_recordMutex);
    if (m_recordedAudioSampleRate <= 0.0) {
        m_recordedAudioSampleRate = sampleRate;
    }

    const int oldSize = m_recordedAudioL.size();
    m_recordedAudioL.resize(oldSize + numFrames);
    m_recordedAudioR.resize(oldSize + numFrames);
    std::memcpy(m_recordedAudioL.data() + oldSize, left, numFrames * sizeof(float));
    std::memcpy(m_recordedAudioR.data() + oldSize, right, numFrames * sizeof(float));
}

void PlaybackController::handleMidiMessage(const MidiInputDevice::Message& msg)
{
    const uint8_t type = msg.type;
    const int boundedPitch = qBound(0, msg.pitch, 127);
    const int boundedVelocity = qBound(0, msg.velocity, 127);

    // 録音中のMIDIイベント処理（ノート＋CC/PitchBend/Aftertouch）
    if (m_isRecording.load() && m_recordingMode == RecordingMode::Midi) {
        const qint64 absoluteTick =
            std::max<qint64>(m_recordStartTick,
                             static_cast<qint64>(m_playPositionTicks.load()));
        const qint64 relativeTick = std::max<qint64>(0, absoluteTick - m_recordStartTick);

        QMutexLocker locker(&m_recordMutex);

        if (type == 0) {
            // NoteOn → アクティブノートに追加
            ActiveRecordedMidiNote active;
            active.pitch = boundedPitch;
            active.startTick = relativeTick;
            active.velocity = qMax(1, boundedVelocity);
            m_activeRecordedMidiNotes.push_back(active);
        } else if (type == 1) {
            // NoteOff → ノート確定
            for (int i = m_activeRecordedMidiNotes.size() - 1; i >= 0; --i) {
                if (m_activeRecordedMidiNotes.at(i).pitch != boundedPitch) {
                    continue;
                }

                const ActiveRecordedMidiNote active = m_activeRecordedMidiNotes.takeAt(i);
                RecordedMidiNote note;
                note.pitch = active.pitch;
                note.startTick = active.startTick;
                note.durationTicks = std::max<qint64>(1, relativeTick - active.startTick);
                note.velocity = active.velocity;
                m_recordedMidiNotes.push_back(note);
                break;
            }
        } else if (type == 2) {
            // CC → 録音データに追加
            RecordedCCEvent ccEv;
            ccEv.ccNumber = qBound(0, static_cast<int>(msg.ccNumber), 127);
            ccEv.tick = relativeTick;
            ccEv.value = qBound(0, static_cast<int>(msg.ccValue), 127);
            m_recordedCCEvents.push_back(ccEv);
        } else if (type == 3) {
            // Pitch Bend → CC_PITCH_BEND として記録
            RecordedCCEvent ccEv;
            ccEv.ccNumber = CCEvent::CC_PITCH_BEND;
            ccEv.tick = relativeTick;
            ccEv.value = qBound(0, static_cast<int>(msg.bendValue), 16383);
            m_recordedCCEvents.push_back(ccEv);
        } else if (type == 4) {
            // Channel Aftertouch → CC_CHANNEL_PRESSURE として記録
            RecordedCCEvent ccEv;
            ccEv.ccNumber = CCEvent::CC_CHANNEL_PRESSURE;
            ccEv.tick = relativeTick;
            ccEv.value = qBound(0, static_cast<int>(msg.pressure), 127);
            m_recordedCCEvents.push_back(ccEv);
        }
    }

    QMutexLocker locker(&m_liveMidiMutex);
    if (m_midiMonitorTrackId < 0) {
        return;
    }

    // ライブモニター側は「選択中トラックへ今すぐ鳴らす」ことが目的なので、
    // オーディオスレッドが読める軽量なイベント列へ積むだけに留める。

    if (type == 0 || type == 1) {
        // NoteOn / NoteOff — 重複管理
        bool shouldQueue = false;
        if (type == 0) {
            ++m_liveHeldNoteCounts[boundedPitch];
            shouldQueue = true;
        } else if (m_liveHeldNoteCounts[boundedPitch] > 0) {
            --m_liveHeldNoteCounts[boundedPitch];
            shouldQueue = true;
        }

        if (!shouldQueue) {
            return;
        }

        LiveMidiMessage liveMessage;
        liveMessage.trackId = m_midiMonitorTrackId;
        liveMessage.type = type == 0 ? 0 : 1;
        liveMessage.pitch = static_cast<int16_t>(boundedPitch);
        liveMessage.velocity = type == 0
            ? static_cast<float>(boundedVelocity) / 127.0f
            : 0.0f;
        m_liveMidiMessages.push_back(liveMessage);
    } else if (type == 2) {
        // CC
        LiveMidiMessage liveMessage;
        liveMessage.trackId = m_midiMonitorTrackId;
        liveMessage.type = 2;
        liveMessage.ccNumber = msg.ccNumber;
        liveMessage.ccValue = msg.ccValue;
        m_liveMidiMessages.push_back(liveMessage);
    } else if (type == 3) {
        // Pitch Bend
        LiveMidiMessage liveMessage;
        liveMessage.trackId = m_midiMonitorTrackId;
        liveMessage.type = 3;
        liveMessage.bendValue = msg.bendValue;
        m_liveMidiMessages.push_back(liveMessage);
    } else if (type == 4) {
        // Channel Aftertouch → CC 129 (CC_CHANNEL_PRESSURE)
        LiveMidiMessage liveMessage;
        liveMessage.trackId = m_midiMonitorTrackId;
        liveMessage.type = 2;
        liveMessage.ccNumber = 129; // 内部的にChannel Aftertouch用のCC番号
        liveMessage.ccValue = msg.pressure;
        m_liveMidiMessages.push_back(liveMessage);
    }
}

void PlaybackController::updateMidiInputState()
{
    // MIDI入力は「MIDI録音中」または「ライブモニター対象トラックあり」のどちらかで生かす。
    // 録音停止後もモニター対象が残っていれば、デバイスは閉じずにそのまま維持する。
    const bool wantsMidiInput =
        (m_isRecording.load() && m_recordingMode == RecordingMode::Midi) ||
        !m_midiMonitorTrack.isNull();

    if (wantsMidiInput) {
        m_midiInputDevice->start();
    } else {
        m_midiInputDevice->stop();
    }
}

Clip* PlaybackController::finalizeAudioRecordingLocked(qint64 recordEndTick)
{
    if (!m_recordTrack) {
        return nullptr;
    }

    QVector<float> recordedL;
    QVector<float> recordedR;
    double sampleRate = 0.0;
    {
        QMutexLocker locker(&m_recordMutex);
        recordedL = m_recordedAudioL;
        recordedR = m_recordedAudioR;
        sampleRate = m_recordedAudioSampleRate;
        m_recordedAudioL.clear();
        m_recordedAudioR.clear();
        m_recordedAudioSampleRate = 0.0;
    }

    const qint64 durationTicks = std::max<qint64>(1, recordEndTick - m_recordStartTick);

    if (recordedL.isEmpty() || recordedR.isEmpty()) {
        const int fallbackFrames = sampleRate > 0.0
            ? std::max<int>(1, static_cast<int>((durationTicks / (m_project->bpm() * Project::TICKS_PER_BEAT / 60.0)) * sampleRate))
            : 1;
        recordedL.fill(0.0f, fallbackFrames);
        recordedR.fill(0.0f, fallbackFrames);
    }

    if (sampleRate <= 0.0) {
        sampleRate = m_audioInputCapture->sampleRate();
    }

    // 録音テイクはクリップ内PCMだけでなく WAV も保存しておき、
    // 後続の保存/再読み込みや波形表示で再利用できるようにする。
    QString filePath;
    QString errorMessage;
    const QString candidatePath = makeRecordingFilePath();
    if (sampleRate > 0.0 &&
        WavWriter::writeStereo16(candidatePath, recordedL, recordedR,
                                 static_cast<int>(sampleRate), &errorMessage)) {
        filePath = candidatePath;
    } else if (!errorMessage.isEmpty()) {
        qWarning() << "PlaybackController: 録音 WAV の保存に失敗:" << errorMessage;
    }

    Clip* clip = m_recordTrack->addClip(m_recordStartTick, durationTicks);
    clip->setAudioData(recordedL, recordedR, sampleRate, filePath);
    return clip;
}

Clip* PlaybackController::finalizeMidiRecordingLocked(qint64 recordEndTick)
{
    if (!m_recordTrack) {
        return nullptr;
    }

    QVector<RecordedMidiNote> finishedNotes;
    QVector<RecordedCCEvent> finishedCCEvents;
    {
        QMutexLocker locker(&m_recordMutex);
        finishedNotes = m_recordedMidiNotes;
        for (const ActiveRecordedMidiNote& active : std::as_const(m_activeRecordedMidiNotes)) {
            RecordedMidiNote note;
            note.pitch = active.pitch;
            note.startTick = active.startTick;
            note.durationTicks =
                std::max<qint64>(1, (recordEndTick - m_recordStartTick) - active.startTick);
            note.velocity = active.velocity;
            finishedNotes.push_back(note);
        }
        m_recordedMidiNotes.clear();
        m_activeRecordedMidiNotes.clear();
        finishedCCEvents = std::move(m_recordedCCEvents);
    }

    const qint64 durationTicks = std::max<qint64>(1, recordEndTick - m_recordStartTick);
    // ノートが1つも無いテイクでも、録音した長さ自体はクリップとして残す。
    Clip* clip = m_recordTrack->addClip(m_recordStartTick, durationTicks);
    for (const RecordedMidiNote& note : std::as_const(finishedNotes)) {
        clip->addNote(note.pitch, note.startTick, note.durationTicks, note.velocity);
    }
    // 録音されたCC/PitchBend/Aftertouchイベントをクリップに追加
    for (const RecordedCCEvent& ccEv : std::as_const(finishedCCEvents)) {
        clip->addCCEvent(ccEv.ccNumber, ccEv.tick, ccEv.value);
    }
    return clip;
}

void PlaybackController::updateUiTimerInterval()
{
    if (!m_uiTimer) {
        return;
    }

    const int interval = m_isPlaying.load() ? UI_TIMER_INTERVAL_PLAYING_MS : UI_TIMER_INTERVAL_IDLE_MS;
    if (m_uiTimer->interval() != interval) {
        m_uiTimer->setInterval(interval);
    }
}

void PlaybackController::suspendForExport()
{
    if (m_isRecording.load()) {
        stopRecording();
    }
    // 再生中なら停止
    if (m_isPlaying.load()) {
        stop();
    }
    // UIタイマーを停止（ensurePluginsPrepared の再呼び出しを防ぐ）
    m_uiTimer->stop();
    // AudioEngine を停止（レンダーコールバックがプラグインにアクセスしないようにする）
    m_audioEngine->stop();
    qDebug() << "PlaybackController: エクスポート用に一時停止";
}

void PlaybackController::resumeFromExport()
{
    // ダイアログ類が完全に閉じ、UIイベントループが落ち着いた後にプラグインとAudioEngineを復旧させる
    // これにより、COMのマーシャリングやメッセージキューの詰まりによるデッドロックを防ぐ
    QTimer::singleShot(50, this, [this]() {
        // AudioEngineを再開する前に、プラグインの状態（サンプルレート等）を安全に復元する
        ensurePluginsPrepared();

        // AudioEngine を再開
        if (!m_audioEngine->isRunning()) {
            m_audioEngine->start();
        }
        // UIタイマーを再開
        updateUiTimerInterval();
        m_uiTimer->start();
        qDebug() << "PlaybackController: エクスポートから復帰";
    });
}

void PlaybackController::onUiTimerTick()
{
    if (!m_project) return;

    const bool isPlaying = m_isPlaying.load();
    // 停止中は重い保守処理を毎回は走らせず、数tickごとにまとめて行う。
    const bool shouldRunMaintenance =
        isPlaying || (++m_idleMaintenanceTick >= IDLE_MAINTENANCE_INTERVAL_TICKS);
    if (shouldRunMaintenance) {
        m_idleMaintenanceTick = 0;
        ensurePluginsPrepared();
    }

    // 再生中のみプレイヘッド位置を更新
    if (isPlaying) {
        qint64 pos = static_cast<qint64>(m_playPositionTicks.load());
        m_project->setPlayheadPosition(pos);
        emit positionChanged(pos);
    }

    // レベルメーター情報は常にUIへ通知（停止中のプレビュー音もメーターに反映）
    float pL = m_peakL.exchange(0.0f);
    float pR = m_peakR.exchange(0.0f);
    emit masterLevelChanged(pL, pR);
    
    // 各トラックのレベルメーター情報
    for (int i = 0; i < m_project->tracks().size(); ++i) {
        if (i < static_cast<int>(m_trackPeakL.size())) {
            float trL = m_trackPeakL[i].exchange(0.0f);
            float trR = m_trackPeakR[i].exchange(0.0f);
            emit trackLevelChanged(i, trL, trR);
        }
    }

    if (shouldRunMaintenance) {
        // プラグインからのリスタート要求も同じ保守タイミングでまとめて確認する。
        for (Track* track : m_project->tracks()) {
            bool requiresRestart = false;
            if (track->hasPlugin() && track->pluginInstance()->isLoaded()) {
                if (track->pluginInstance()->consumeRestartFlags() != 0) {
                    requiresRestart = true;
                }
            }
            if (!requiresRestart) {
                for (VST3PluginInstance* fx : track->fxPlugins()) {
                    if (fx && fx->isLoaded() && fx->consumeRestartFlags() != 0) {
                        requiresRestart = true;
                        break;
                    }
                }
            }
            
            if (requiresRestart) {
                qDebug() << "PlaybackController: Plugin restart required from track:" << track->name();
                // リスタート要求で強制的に先頭に戻すのは、UIスレッドをブロックする危険があるため行わない
                // VSTパラメータ名等のGUI更新要求はフラグ消費で完了とする
                // seekTo(0);
                break;
            }
        }
    }
}

void PlaybackController::audioRenderCallback(float* outputBuffer, int numFrames,
                                              int numChannels, double sampleRate)
{
    // ゼロクリア
    memset(outputBuffer, 0, numFrames * numChannels * sizeof(float));

    QMutexLocker<QRecursiveMutex> modelLock(&Darwin::modelAccessMutex());

    if (!m_project) {
        return;
    }

    bool playing = m_isPlaying.load();

    double bpm = m_project->bpm();
    double ticksPerSecond = bpm * Project::TICKS_PER_BEAT / 60.0;
    double ticksPerSample = ticksPerSecond / sampleRate;

    double startTick = m_playPositionTicks.load();
    double endTick = startTick + numFrames * ticksPerSample;

    // ミキシングバッファを準備
    if (static_cast<int>(m_mixBufL.size()) < numFrames) {
        m_mixBufL.resize(numFrames);
        m_mixBufR.resize(numFrames);
        m_trackBufL.resize(numFrames);
        m_trackBufR.resize(numFrames);
    }
    memset(m_mixBufL.data(), 0, numFrames * sizeof(float));
    memset(m_mixBufR.data(), 0, numFrames * sizeof(float));

    // 各トラックを処理
    const auto& tracks = m_project->tracks();

    if (m_routingCacheDirty.exchange(false)) {
        rebuildRoutingCacheLocked();
    }
    
    // トランスポート情報を構築（全トラック共通）
    VST3PluginInstance::TransportInfo transport {};
    transport.positionInTicks = startTick;
    transport.bpm = bpm;
    transport.sampleRate = sampleRate;
    transport.isPlaying = playing;
    transport.timeSigNumerator = 4;   // TODO: プロジェクトの拍子設定から取得
    transport.timeSigDenominator = 4;
    transport.ticksPerBeat = Project::TICKS_PER_BEAT;

    QVector<LiveMidiMessage> liveMidiMessages;
    {
        QMutexLocker locker(&m_liveMidiMutex);
        if (!m_liveMidiMessages.isEmpty()) {
            // ライブMIDIはUIスレッド/WinMMコールバック側から積まれるので、
            // バッファ先頭でまとめてスワップし、以降はオーディオスレッド専用で扱う。
            liveMidiMessages = m_liveMidiMessages;
            m_liveMidiMessages.clear();
        }
    }

    // ── フォルダバスの初期化 ──
    // フォルダトラックごとにバスバッファを確保・ゼロクリア
    for (int ti = 0; ti < tracks.size(); ++ti) {
        Track* track = tracks[ti];
        if (track->isFolder()) {
            auto& bus = m_folderBuses[track->id()];
            if (static_cast<int>(bus.bufL.size()) < numFrames) {
                bus.bufL.resize(numFrames);
                bus.bufR.resize(numFrames);
            }
            memset(bus.bufL.data(), 0, numFrames * sizeof(float));
            memset(bus.bufR.data(), 0, numFrames * sizeof(float));
        }
    }

    // ── Phase 1: 非フォルダトラックのオーディオ処理 ──
    // 各トラックの出力を、親フォルダがあればフォルダバスへ、なければマスターバスへルーティング
    for (int ti = 0; ti < tracks.size(); ++ti) {
        Track* track = tracks[ti];
        if (track->isFolder()) continue; // フォルダはPhase 2で処理
        if (track->isMuted()) continue;

        // タイミングオフセット: msをtickに変換
        // 正のオフセット = 遅らせる = トラックの再生位置を前にシフト
        double offsetTicks = track->timingOffsetMs() * ticksPerSecond / 1000.0;
        double trackStartTick = startTick - offsetTicks;
        double trackEndTick = endTick - offsetTicks;

        // トラックのプラグインミューテックスを非ブロッキングで取得
        if (!track->pluginMutex().tryLock()) continue;

        bool hasInstrument = track->hasPlugin() &&
                             track->pluginInstance()->isLoaded() &&
                             track->pluginInstance()->isAudioPrepared();

        // トラック内にオーディオクリップが存在するか確認
        bool hasAudioClips = false;
        for (const Clip* clip : track->clips()) {
            if (clip->isAudioClip() && !clip->audioSamplesL().isEmpty()) {
                hasAudioClips = true;
                break;
            }
        }

        // プラグインもオーディオクリップも無い場合はスキップ
        if (!hasInstrument && !hasAudioClips) {
            track->pluginMutex().unlock();
            continue;
        }

        // トラックバッファをゼロクリア
        memset(m_trackBufL.data(), 0, numFrames * sizeof(float));
        memset(m_trackBufR.data(), 0, numFrames * sizeof(float));

        // ─── オーディオクリップの処理（再生中のみ） ───
        if (playing && hasAudioClips) {
            for (const Clip* clip : track->clips()) {
                if (!clip->isAudioClip() || clip->audioSamplesL().isEmpty()) continue;

                double clipStartTick = static_cast<double>(clip->startTick());
                double clipEndTick = static_cast<double>(clip->endTick());

                // タイミングオフセット適用済みのtick範囲で判定
                if (trackEndTick <= clipStartTick || trackStartTick >= clipEndTick) continue;

                // オーディオサンプルレートとプロジェクトのtick変換
                double audioSR = clip->audioSampleRate();
                const QVector<float>& samplesL = clip->audioSamplesL();
                const QVector<float>& samplesR = clip->audioSamplesR();
                int totalAudioFrames = samplesL.size();

                for (int f = 0; f < numFrames; ++f) {
                    double currentTickPos = trackStartTick + f * ticksPerSample;
                    if (currentTickPos < clipStartTick || currentTickPos >= clipEndTick) continue;

                    // クリップ内のtick位置 → 秒 → オーディオサンプルインデックス
                    double tickInClip = currentTickPos - clipStartTick;
                    double secondsInClip = tickInClip / ticksPerSecond;
                    double audioSamplePos = secondsInClip * audioSR;

                    int idx = static_cast<int>(audioSamplePos);
                    if (idx < 0 || idx >= totalAudioFrames) continue;

                    // 線形補間
                    float frac = static_cast<float>(audioSamplePos - idx);
                    int idx1 = qMin(idx + 1, totalAudioFrames - 1);

                    float sL = samplesL[idx] * (1.0f - frac) + samplesL[idx1] * frac;
                    float sR = samplesR[idx] * (1.0f - frac) + samplesR[idx1] * frac;

                    m_trackBufL[f] += sL;
                    m_trackBufR[f] += sR;
                }
            }
        }

        // ─── MIDIクリップの処理（インストゥルメントプラグインあり時） ───
        if (hasInstrument) {
            // このトラックのMIDIイベントを収集（再生中のみ）
            std::vector<VST3PluginInstance::MidiEvent> trackEvents;

            if (playing) {
                // 1) アクティブノートのNoteOffチェック
                for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ) {
                    if (it->trackIndex == ti && it->endTick >= trackStartTick && it->endTick < trackEndTick) {
                        int sampleOffset = static_cast<int>((it->endTick - trackStartTick) / ticksPerSample);
                        sampleOffset = std::clamp(sampleOffset, 0, numFrames - 1);

                        VST3PluginInstance::MidiEvent noteOff {};
                        noteOff.sampleOffset = sampleOffset;
                        noteOff.type = 1;
                        noteOff.pitch = it->pitch;
                        noteOff.velocity = 0.0f;
                        trackEvents.push_back(noteOff);

                        it = m_activeNotes.erase(it);
                    } else {
                        ++it;
                    }
                }

                // 2) MIDIクリップからのNoteOnイベント（オーディオクリップは除外）
                for (const Clip* clip : track->clips()) {
                    if (clip->isAudioClip()) continue; // オーディオクリップはMIDI処理不要

                    double clipStart = static_cast<double>(clip->startTick());
                    double clipEnd = static_cast<double>(clip->endTick());

                    if (trackEndTick < clipStart || trackStartTick >= clipEnd) continue;

                    for (const Note* note : clip->notes()) {
                        double noteStart = clipStart + static_cast<double>(note->startTick());
                        double noteEnd = noteStart + static_cast<double>(note->durationTicks());

                        if (noteStart >= trackStartTick && noteStart < trackEndTick) {
                            int sampleOffset = static_cast<int>((noteStart - trackStartTick) / ticksPerSample);
                            sampleOffset = std::clamp(sampleOffset, 0, numFrames - 1);

                            VST3PluginInstance::MidiEvent noteOn {};
                            noteOn.sampleOffset = sampleOffset;
                            noteOn.type = 0;
                            noteOn.pitch = static_cast<int16_t>(note->pitch());
                            noteOn.velocity = static_cast<float>(note->velocity()) / 127.0f;
                            trackEvents.push_back(noteOn);

                            ActiveNote an {};
                            an.trackIndex = ti;
                            an.pitch = static_cast<int16_t>(note->pitch());
                            an.endTick = noteEnd;
                            m_activeNotes.push_back(an);
                        }
                    }
                }

                // 3) CCオートメーションイベントの収集
                for (const Clip* clip : track->clips()) {
                    if (clip->isAudioClip()) continue;

                    double clipStart = static_cast<double>(clip->startTick());
                    double clipEnd = static_cast<double>(clip->endTick());
                    if (trackEndTick < clipStart || trackStartTick >= clipEnd) continue;

                    for (const CCEvent* ccEv : clip->ccEvents()) {
                        double evTick = clipStart + static_cast<double>(ccEv->tick());
                        if (evTick >= trackStartTick && evTick < trackEndTick) {
                            int sampleOffset = static_cast<int>((evTick - trackStartTick) / ticksPerSample);
                            sampleOffset = std::clamp(sampleOffset, 0, numFrames - 1);

                            VST3PluginInstance::MidiEvent ccMidi {};
                            ccMidi.sampleOffset = sampleOffset;

                            if (ccEv->ccNumber() == CCEvent::CC_PITCH_BEND) {
                                ccMidi.type = 3; // Pitch Bend
                                ccMidi.bendValue = static_cast<int16_t>(ccEv->value());
                            } else {
                                ccMidi.type = 2; // CC
                                ccMidi.ccNumber = static_cast<uint8_t>(ccEv->ccNumber());
                                ccMidi.ccValue = static_cast<uint8_t>(ccEv->value());
                            }
                            trackEvents.push_back(ccMidi);
                        }
                    }
                }

            }

            for (const LiveMidiMessage& liveMessage : std::as_const(liveMidiMessages)) {
                if (liveMessage.trackId != track->id()) {
                    continue;
                }

                // 物理MIDI入力は再生状態に関係なくここへ混ぜ込む。
                // これにより、停止中でも選択トラックの音源を鍵盤で試奏できる。
                VST3PluginInstance::MidiEvent liveEvent {};
                liveEvent.sampleOffset = std::clamp(liveMessage.sampleOffset, 0, numFrames - 1);
                liveEvent.type = liveMessage.type;
                liveEvent.pitch = liveMessage.pitch;
                liveEvent.velocity = liveMessage.velocity;
                liveEvent.ccNumber = liveMessage.ccNumber;
                liveEvent.ccValue = liveMessage.ccValue;
                liveEvent.bendValue = liveMessage.bendValue;
                trackEvents.push_back(liveEvent);
            }

            if (!trackEvents.empty()) {
                std::sort(trackEvents.begin(), trackEvents.end(),
                          [](const auto& a, const auto& b) {
                              return a.sampleOffset < b.sampleOffset;
                          });
            }

            // インストゥルメントプラグインでオーディオ処理
            // オーディオクリップがある場合は一時バッファを使いミックスする
            if (hasAudioClips) {
                if (static_cast<int>(m_pluginBufL.size()) < numFrames) {
                    m_pluginBufL.resize(numFrames);
                    m_pluginBufR.resize(numFrames);
                }
                memset(m_pluginBufL.data(), 0, numFrames * sizeof(float));
                memset(m_pluginBufR.data(), 0, numFrames * sizeof(float));
                track->pluginInstance()->processAudio(
                    nullptr, nullptr, m_pluginBufL.data(), m_pluginBufR.data(),
                    numFrames, trackEvents, transport);

                // プラグイン出力をオーディオクリップ出力にミックス
                for (int i = 0; i < numFrames; ++i) {
                    m_trackBufL[i] += m_pluginBufL[i];
                    m_trackBufR[i] += m_pluginBufR[i];
                }
            } else {
                track->pluginInstance()->processAudio(
                    nullptr, nullptr, m_trackBufL.data(), m_trackBufR.data(),
                    numFrames, trackEvents, transport);
            }
        }

        // トラック固有のFXインサート処理 (カスケード)
        for (VST3PluginInstance* fx : track->fxPlugins()) {
            if (fx && fx->isLoaded() && fx->isAudioPrepared()) {
                fx->processAudio(
                    m_trackBufL.data(), m_trackBufR.data(),
                    m_trackBufL.data(), m_trackBufR.data(),
                    numFrames, {}, transport
                );
            }
        }

        // トラックのボリューム・パン適用
        double vol = track->volume();
        double pan = track->pan();
        double gainL = vol * std::min(1.0, 1.0 - pan);
        double gainR = vol * std::min(1.0, 1.0 + pan);

        float trackPeakL = 0.0f;
        float trackPeakR = 0.0f;

        // 出力先を決定: 親フォルダがあればフォルダバス、なければマスターバス
        int parentFolderId = track->parentFolderId();
        float* destL = m_mixBufL.data();
        float* destR = m_mixBufR.data();
        if (parentFolderId >= 0) {
            auto busIt = m_folderBuses.find(parentFolderId);
            if (busIt != m_folderBuses.end()) {
                destL = busIt->second.bufL.data();
                destR = busIt->second.bufR.data();
            }
        }

        for (int i = 0; i < numFrames; ++i) {
            float outL = m_trackBufL[i] * static_cast<float>(gainL);
            float outR = m_trackBufR[i] * static_cast<float>(gainR);
            destL[i] += outL;
            destR[i] += outR;
            
            trackPeakL = std::max(trackPeakL, std::abs(outL));
            trackPeakR = std::max(trackPeakR, std::abs(outR));
        }
        
        // トラックのピーク値をアトミック変数に保存
        if (ti < static_cast<int>(m_trackPeakL.size())) {
            float prevTrPeakL = m_trackPeakL[ti].load();
            while (prevTrPeakL < trackPeakL && !m_trackPeakL[ti].compare_exchange_weak(prevTrPeakL, trackPeakL)) {}
            
            float prevTrPeakR = m_trackPeakR[ti].load();
            while (prevTrPeakR < trackPeakR && !m_trackPeakR[ti].compare_exchange_weak(prevTrPeakR, trackPeakR)) {}
        }

        track->pluginMutex().unlock();
    }

    // ── Phase 2: フォルダトラックの処理（深いフォルダから順に） ──
    // フォルダバスにFXを適用 → ボリューム/パン → 親フォルダバスまたはマスターバスへ
    for (int ti : m_sortedFolderTrackIndices) {
        Track* folder = tracks[ti];
        auto busIt = m_folderBuses.find(folder->id());
        if (busIt == m_folderBuses.end()) continue;

        auto& bus = busIt->second;

        // フォルダのFXインサート処理
        if (folder->pluginMutex().tryLock()) {
            for (VST3PluginInstance* fx : folder->fxPlugins()) {
                if (fx && fx->isLoaded() && fx->isAudioPrepared()) {
                    fx->processAudio(
                        bus.bufL.data(), bus.bufR.data(),
                        bus.bufL.data(), bus.bufR.data(),
                        numFrames, {}, transport
                    );
                }
            }
            folder->pluginMutex().unlock();
        }

        // フォルダのボリューム・パン適用
        double vol = folder->volume();
        double pan = folder->pan();
        double gainL = vol * std::min(1.0, 1.0 - pan);
        double gainR = vol * std::min(1.0, 1.0 + pan);

        float folderPeakL = 0.0f;
        float folderPeakR = 0.0f;

        // 出力先を決定: 親フォルダがあればそのバス、なければマスターバス
        int parentFolderId = folder->parentFolderId();
        float* destL = m_mixBufL.data();
        float* destR = m_mixBufR.data();
        if (parentFolderId >= 0) {
            auto parentBusIt = m_folderBuses.find(parentFolderId);
            if (parentBusIt != m_folderBuses.end()) {
                destL = parentBusIt->second.bufL.data();
                destR = parentBusIt->second.bufR.data();
            }
        }

        for (int i = 0; i < numFrames; ++i) {
            float outL = bus.bufL[i] * static_cast<float>(gainL);
            float outR = bus.bufR[i] * static_cast<float>(gainR);
            destL[i] += outL;
            destR[i] += outR;

            folderPeakL = std::max(folderPeakL, std::abs(outL));
            folderPeakR = std::max(folderPeakR, std::abs(outR));
        }

        // フォルダのピーク値を保存（メータリング用）
        if (ti < static_cast<int>(m_trackPeakL.size())) {
            float prev = m_trackPeakL[ti].load();
            while (prev < folderPeakL && !m_trackPeakL[ti].compare_exchange_weak(prev, folderPeakL)) {}

            prev = m_trackPeakR[ti].load();
            while (prev < folderPeakR && !m_trackPeakR[ti].compare_exchange_weak(prev, folderPeakR)) {}
        }
    }

    // インターリーブして出力バッファに書き込み、同時にピーク値を計算
    float currentPeakL = 0.0f;
    float currentPeakR = 0.0f;

    // Master FX 処理
    if (m_project && m_project->masterTrack()) {
        Track* masterTrack = m_project->masterTrack();
        if (masterTrack->pluginMutex().tryLock()) {
            const auto& masterFx = masterTrack->fxPlugins();
            for (VST3PluginInstance* fx : masterFx) {
                if (fx && fx->isLoaded() && fx->isAudioPrepared()) {
                    fx->processAudio(
                        m_mixBufL.data(), m_mixBufR.data(),
                        m_mixBufL.data(), m_mixBufR.data(),
                        numFrames,
                        {},
                        transport
                    );
                }
            }
            masterTrack->pluginMutex().unlock();
        }
    }

    // マスターボリューム・パンの適用
    double masterVol = 1.0;
    double masterPan = 0.0;
    if (m_project && m_project->masterTrack()) {
        masterVol = m_project->masterTrack()->volume();
        masterPan = m_project->masterTrack()->pan();
    }
    float masterGainL = static_cast<float>(masterVol * std::min(1.0, 1.0 - masterPan));
    float masterGainR = static_cast<float>(masterVol * std::min(1.0, 1.0 + masterPan));

    if (numChannels >= 2) {
        for (int i = 0; i < numFrames; ++i) {
            float outL = m_mixBufL[i] * masterGainL;
            float outR = m_mixBufR[i] * masterGainR;
            
            outputBuffer[i * numChannels + 0] = outL;
            outputBuffer[i * numChannels + 1] = outR;
            
            currentPeakL = std::max(currentPeakL, std::abs(outL));
            currentPeakR = std::max(currentPeakR, std::abs(outR));
            // 3ch以降はゼロ（既にクリア済み）
        }
    } else if (numChannels == 1) {
        // モノラル: L+Rの平均
        for (int i = 0; i < numFrames; ++i) {
            float outM = (m_mixBufL[i] * masterGainL + m_mixBufR[i] * masterGainR) * 0.5f;
            outputBuffer[i] = outM;
            currentPeakL = std::max(currentPeakL, std::abs(outM));
            currentPeakR = currentPeakL;
        }
    }

    // ピーク値をアトミック変数に記録（UIタイマーで拾うまで高い値を維持）
    float prevPeakL = m_peakL.load();
    while (prevPeakL < currentPeakL && !m_peakL.compare_exchange_weak(prevPeakL, currentPeakL)) {}
    
    float prevPeakR = m_peakR.load();
    while (prevPeakR < currentPeakR && !m_peakR.compare_exchange_weak(prevPeakR, currentPeakR)) {}

    // 再生位置とアクティブノートの更新（再生中のみ）
    if (playing) {
        double nextPosition = endTick;
        qint64 expEnd = m_project->exportEndTick();
        
        // エクスポート範囲が設定されており、終端に達した場合はループ
        if (expEnd != -1 && nextPosition >= static_cast<double>(expEnd)) {
            nextPosition = static_cast<double>(m_project->exportStartTick());
            m_activeNotes.clear(); // ループ時の音残りを防ぐ
        }

        m_playPositionTicks.store(nextPosition);

        // 終了ティックまでのアクティブノートでもう終わったものを除去
        if (!m_activeNotes.empty()) {
            m_activeNotes.erase(
                std::remove_if(m_activeNotes.begin(), m_activeNotes.end(),
                               [endTick](const ActiveNote& an) { return an.endTick < endTick; }),
                m_activeNotes.end());
        }
    }
}

void PlaybackController::ensurePluginsPrepared()
{
    if (!m_project) return;

    double sr = m_audioEngine->sampleRate();
    int blockSize = m_audioEngine->bufferSize();
    if (blockSize <= 0) blockSize = 1024;

    for (Track* track : m_project->tracks()) {
        ensureTrackPluginsPrepared(track, sr, blockSize);
    }
    
    // Master FXプラグイン
    if (m_project->masterTrack()) {
        ensureTrackPluginsPrepared(m_project->masterTrack(), sr, blockSize);
    }
}

void PlaybackController::ensureTrackPluginsPrepared(Track* track, double sr, int blockSize)
{
    if (!track) return;

    // プラグインの追加/削除・オーディオ処理との競合を避けるためtryLockを使用
    // （デッドロック防止：AudioスレッドがVST側のUIメッセージ待ちになっている時にブロックしないように）
    if (!track->pluginMutex().tryLock()) return;
    
    // インストゥルメントプラグイン
    if (track->hasPlugin() && track->pluginInstance()->isLoaded()) {
        if (!track->pluginInstance()->isAudioPrepared() ||
            track->pluginInstance()->currentSampleRate() != sr) {
            track->pluginInstance()->prepareAudio(sr, blockSize);
            qDebug() << "PlaybackController: プラグインを自動準備:" << track->pluginInstance()->pluginName();
        }
    }
    // FXプラグイン
    for (VST3PluginInstance* fx : track->fxPlugins()) {
        if (fx && fx->isLoaded()) {
            if (!fx->isAudioPrepared() || fx->currentSampleRate() != sr) {
                fx->prepareAudio(sr, blockSize);
                qDebug() << "PlaybackController: FXプラグインを自動準備:" << fx->pluginName();
            }
        }
    }
    
    track->pluginMutex().unlock();
}
