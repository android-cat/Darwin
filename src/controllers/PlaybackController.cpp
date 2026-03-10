#include "PlaybackController.h"
#include "AudioEngine.h"
#include "Project.h"
#include "Track.h"
#include "Clip.h"
#include "Note.h"
#include "VST3PluginInstance.h"
#include <QDebug>
#include <QMutexLocker>
#include <algorithm>
#include <cstring>
#include "common/ModelAccessLock.h"

PlaybackController::PlaybackController(Project* project, QObject* parent)
    : QObject(parent)
    , m_project(project)
    , m_audioEngine(new AudioEngine(this))
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

    // UI更新タイマー（60fps）— プレイヘッド位置・レベルメーター・プラグイン準備チェック
    m_uiTimer->setInterval(16);
    connect(m_uiTimer, &QTimer::timeout, this, &PlaybackController::onUiTimerTick);
    m_uiTimer->start(); // 常時稼働
}

PlaybackController::~PlaybackController()
{
    stop();
    m_uiTimer->stop();
    m_audioEngine->stop();
}

void PlaybackController::setProject(Project* project)
{
    if (m_project != project) {
        stop();
        m_project = project;
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

    emit playStateChanged(false);
}

void PlaybackController::stop()
{
    bool wasPlaying = m_isPlaying.load();

    m_isPlaying.store(false);
    // AudioEngineは停止しない（プラグインGUIプレビューの音を処理し続けるため）
    m_activeNotes.clear();

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

void PlaybackController::suspendForExport()
{
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
        m_uiTimer->start();
        qDebug() << "PlaybackController: エクスポートから復帰";
    });
}

void PlaybackController::onUiTimerTick()
{
    if (!m_project) return;

    // ロード済みで未準備のプラグインを自動的に準備（GUIプレビュー対応）
    ensurePluginsPrepared();

    // 再生中のみプレイヘッド位置を更新
    if (m_isPlaying.load()) {
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

    // プラグインからのリスタート要求をチェック
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
    
    // トランスポート情報を構築（全トラック共通）
    VST3PluginInstance::TransportInfo transport {};
    transport.positionInTicks = startTick;
    transport.bpm = bpm;
    transport.sampleRate = sampleRate;
    transport.isPlaying = playing;
    transport.timeSigNumerator = 4;   // TODO: プロジェクトの拍子設定から取得
    transport.timeSigDenominator = 4;
    transport.ticksPerBeat = Project::TICKS_PER_BEAT;

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

                std::sort(trackEvents.begin(), trackEvents.end(),
                          [](const auto& a, const auto& b) {
                              return a.sampleOffset < b.sampleOffset;
                          });
            }

            // インストゥルメントプラグインでオーディオ処理
            // オーディオクリップがある場合は一時バッファを使いミックスする
            if (hasAudioClips) {
                std::vector<float> pluginBufL(numFrames, 0.0f);
                std::vector<float> pluginBufR(numFrames, 0.0f);
                track->pluginInstance()->processAudio(
                    nullptr, nullptr, pluginBufL.data(), pluginBufR.data(),
                    numFrames, trackEvents, transport);

                // プラグイン出力をオーディオクリップ出力にミックス
                for (int i = 0; i < numFrames; ++i) {
                    m_trackBufL[i] += pluginBufL[i];
                    m_trackBufR[i] += pluginBufR[i];
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
    std::vector<std::pair<int, int>> foldersByDepth; // (depth, trackIndex)
    for (int ti = 0; ti < tracks.size(); ++ti) {
        if (tracks[ti]->isFolder()) {
            // 深さを計算（parentFolderIdを辿る）
            int depth = 0;
            int fid = tracks[ti]->parentFolderId();
            while (fid >= 0) {
                depth++;
                Track* parent = m_project->trackById(fid);
                fid = parent ? parent->parentFolderId() : -1;
            }
            foldersByDepth.push_back({depth, ti});
        }
    }
    // 深い順にソート
    std::sort(foldersByDepth.begin(), foldersByDepth.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    for (auto& [depth, ti] : foldersByDepth) {
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

    if (numChannels >= 2) {
        for (int i = 0; i < numFrames; ++i) {
            float outL = m_mixBufL[i];
            float outR = m_mixBufR[i];
            
            outputBuffer[i * numChannels + 0] = outL;
            outputBuffer[i * numChannels + 1] = outR;
            
            currentPeakL = std::max(currentPeakL, std::abs(outL));
            currentPeakR = std::max(currentPeakR, std::abs(outR));
            // 3ch以降はゼロ（既にクリア済み）
        }
    } else if (numChannels == 1) {
        // モノラル: L+Rの平均
        for (int i = 0; i < numFrames; ++i) {
            float outM = (m_mixBufL[i] + m_mixBufR[i]) * 0.5f;
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
