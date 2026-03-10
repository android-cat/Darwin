#include "AudioExporter.h"
#include "Project.h"
#include "Track.h"
#include "Clip.h"
#include "Note.h"
#include "VST3PluginInstance.h"

#include <QFile>
#include <QDataStream>
#include <QDebug>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <vector>
#include <unordered_map>

AudioExporter::AudioExporter(QObject* parent)
    : QObject(parent)
{
}

bool AudioExporter::exportToWav(Project* project, const QString& filePath,
                                 double sampleRate, int bitDepth)
{
    // プロジェクトのエクスポート範囲を使用
    qint64 startTick = project ? project->exportStartTick() : 0;
    qint64 endTick = project ? project->exportEndTick() : -1;
    return exportToWav(project, filePath, startTick, endTick, sampleRate, bitDepth);
}

bool AudioExporter::exportToWav(Project* project, const QString& filePath,
                                 qint64 rangeStartTick, qint64 rangeEndTick,
                                 double sampleRate, int bitDepth)
{
    if (!project) {
        emit exportFinished(false, "プロジェクトが指定されていません");
        return false;
    }

    // エクスポート範囲を計算（全クリップの終了位置を取得）
    qint64 maxEndTick = 0;
    for (const Track* track : project->tracks()) {
        for (const Clip* clip : track->clips()) {
            maxEndTick = qMax(maxEndTick, clip->endTick());
        }
    }

    // 範囲指定があれば適用
    qint64 exportStart = (rangeStartTick > 0) ? rangeStartTick : 0;
    qint64 exportEnd = (rangeEndTick > 0) ? rangeEndTick : maxEndTick;
    if (exportEnd <= exportStart) exportEnd = maxEndTick;

    if (exportEnd == 0) {
        emit exportFinished(false, "エクスポートするデータがありません");
        return false;
    }

    // ティックを秒に変換
    double bpm = project->bpm();
    double ticksPerSecond = bpm * Project::TICKS_PER_BEAT / 60.0;
    double rangeDuration = (exportEnd - exportStart) / ticksPerSecond;
    double totalSeconds = rangeDuration;
    // 末尾に2秒のリリースタイムを追加
    totalSeconds += 2.0;

    int numChannels = 2;
    int blockSize = 1024;
    uint32_t totalFrames = static_cast<uint32_t>(totalSeconds * sampleRate);

    qDebug() << "AudioExporter: エクスポート開始 -" << totalSeconds << "秒,"
             << totalFrames << "フレーム," << sampleRate << "Hz"
             << "範囲:" << exportStart << "-" << exportEnd << "tick";

    // 全トラックのプラグインを準備
    for (Track* track : project->tracks()) {
        if (track->hasPlugin() && track->pluginInstance()->isLoaded()) {
            track->pluginInstance()->prepareAudio(sampleRate, blockSize);
        }
        for (VST3PluginInstance* fx : track->fxPlugins()) {
            if (fx && fx->isLoaded()) {
                fx->prepareAudio(sampleRate, blockSize);
            }
        }
    }

    // ファイルを開く
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit exportFinished(false, "ファイルを開けません: " + filePath);
        return false;
    }

    // WAVヘッダーを書き込み（後でサイズを更新）
    writeWavHeader(file, numChannels, sampleRate, bitDepth, totalFrames);

    // オフラインレンダリング
    double ticksPerSample = ticksPerSecond / sampleRate;
    double currentTick = static_cast<double>(exportStart); // 範囲の開始位置から
    uint32_t framesWritten = 0;

    std::vector<float> mixBufL(blockSize, 0.0f);
    std::vector<float> mixBufR(blockSize, 0.0f);
    std::vector<float> trackBufL(blockSize, 0.0f);
    std::vector<float> trackBufR(blockSize, 0.0f);
    std::vector<float> pluginBufL(blockSize, 0.0f);
    std::vector<float> pluginBufR(blockSize, 0.0f);

    struct FolderBus {
        std::vector<float> bufL;
        std::vector<float> bufR;
    };
    std::unordered_map<int, FolderBus> folderBuses;
    std::unordered_map<int, Track*> trackById;
    std::vector<int> sortedFolderTrackIndices;

    const auto& tracks = project->tracks();
    sortedFolderTrackIndices.reserve(static_cast<size_t>(tracks.size()));

    for (int ti = 0; ti < tracks.size(); ++ti) {
        Track* track = tracks[ti];
        if (!track) {
            continue;
        }

        trackById[track->id()] = track;
        if (track->isFolder()) {
            sortedFolderTrackIndices.push_back(ti);
        }
    }

    auto depthForTrack = [&trackById](Track* track) {
        int depth = 0;
        for (int folderId = track ? track->parentFolderId() : -1; folderId >= 0; ) {
            auto it = trackById.find(folderId);
            if (it == trackById.end() || !it->second) {
                break;
            }
            ++depth;
            folderId = it->second->parentFolderId();
        }
        return depth;
    };

    std::sort(sortedFolderTrackIndices.begin(), sortedFolderTrackIndices.end(),
              [&tracks, &depthForTrack](int lhs, int rhs) {
                  return depthForTrack(tracks[lhs]) > depthForTrack(tracks[rhs]);
              });

    // アクティブノート追跡
    struct ActiveNote {
        int trackIndex;
        int16_t pitch;
        double endTick;
    };
    std::vector<ActiveNote> activeNotes;

    while (framesWritten < totalFrames) {
        int framesToProcess = qMin(blockSize, static_cast<int>(totalFrames - framesWritten));
        double startTick = currentTick;
        double endTick = startTick + framesToProcess * ticksPerSample;

        // ミキシングバッファをクリア
        memset(mixBufL.data(), 0, framesToProcess * sizeof(float));
        memset(mixBufR.data(), 0, framesToProcess * sizeof(float));

        // トランスポート情報
        VST3PluginInstance::TransportInfo transport {};
        transport.positionInTicks = startTick;
        transport.bpm = bpm;
        transport.sampleRate = sampleRate;
        transport.isPlaying = true;
        transport.timeSigNumerator = 4;
        transport.timeSigDenominator = 4;
        transport.ticksPerBeat = Project::TICKS_PER_BEAT;

        // ── フォルダバスの初期化 ──
        for (int ti = 0; ti < tracks.size(); ++ti) {
            Track* t = tracks[ti];
            if (t->isFolder()) {
                auto& bus = folderBuses[t->id()];
                if (static_cast<int>(bus.bufL.size()) < framesToProcess) {
                    bus.bufL.resize(framesToProcess);
                    bus.bufR.resize(framesToProcess);
                }
                memset(bus.bufL.data(), 0, framesToProcess * sizeof(float));
                memset(bus.bufR.data(), 0, framesToProcess * sizeof(float));
            }
        }

        // ── Phase 1: 非フォルダトラックの処理 ──
        for (int ti = 0; ti < tracks.size(); ++ti) {
            Track* track = tracks[ti];
            if (track->isFolder()) continue;
            if (track->isMuted()) continue;

            // タイミングオフセット: msをtickに変換
            double offsetTicks = track->timingOffsetMs() * ticksPerSecond / 1000.0;
            double trackStartTick = startTick - offsetTicks;
            double trackEndTick = endTick - offsetTicks;

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
            if (!hasInstrument && !hasAudioClips) continue;

            // トラックバッファをゼロクリア
            memset(trackBufL.data(), 0, framesToProcess * sizeof(float));
            memset(trackBufR.data(), 0, framesToProcess * sizeof(float));

            // ─── オーディオクリップの処理 ───
            if (hasAudioClips) {
                double ticksPerSecond = bpm * Project::TICKS_PER_BEAT / 60.0;
                for (const Clip* clip : track->clips()) {
                    if (!clip->isAudioClip() || clip->audioSamplesL().isEmpty()) continue;

                    double clipStartTick = static_cast<double>(clip->startTick());
                    double clipEndTick = static_cast<double>(clip->endTick());

                    if (trackEndTick <= clipStartTick || trackStartTick >= clipEndTick) continue;

                    double audioSR = clip->audioSampleRate();
                    const QVector<float>& samplesL = clip->audioSamplesL();
                    const QVector<float>& samplesR = clip->audioSamplesR();
                    int totalAudioFrames = samplesL.size();

                    for (int f = 0; f < framesToProcess; ++f) {
                        double currentTickPos = trackStartTick + f * ticksPerSample;
                        if (currentTickPos < clipStartTick || currentTickPos >= clipEndTick) continue;

                        double tickInClip = currentTickPos - clipStartTick;
                        double secondsInClip = tickInClip / ticksPerSecond;
                        double audioSamplePos = secondsInClip * audioSR;

                        int idx = static_cast<int>(audioSamplePos);
                        if (idx < 0 || idx >= totalAudioFrames) continue;

                        float frac = static_cast<float>(audioSamplePos - idx);
                        int idx1 = qMin(idx + 1, totalAudioFrames - 1);

                        float sL = samplesL[idx] * (1.0f - frac) + samplesL[idx1] * frac;
                        float sR = samplesR[idx] * (1.0f - frac) + samplesR[idx1] * frac;

                        trackBufL[f] += sL;
                        trackBufR[f] += sR;
                    }
                }
            }

            // ─── MIDIクリップの処理（インストゥルメントプラグインあり時） ───
            if (hasInstrument) {
                // MIDIイベント収集
                std::vector<VST3PluginInstance::MidiEvent> trackEvents;

                // NoteOff チェック
                for (auto it = activeNotes.begin(); it != activeNotes.end(); ) {
                    if (it->trackIndex == ti && it->endTick >= trackStartTick && it->endTick < trackEndTick) {
                        int sampleOffset = static_cast<int>((it->endTick - trackStartTick) / ticksPerSample);
                        sampleOffset = std::clamp(sampleOffset, 0, framesToProcess - 1);

                        VST3PluginInstance::MidiEvent noteOff {};
                        noteOff.sampleOffset = sampleOffset;
                        noteOff.type = 1;
                        noteOff.pitch = it->pitch;
                        noteOff.velocity = 0.0f;
                        trackEvents.push_back(noteOff);
                        it = activeNotes.erase(it);
                    } else {
                        ++it;
                    }
                }

                // NoteOn収集（オーディオクリップは除外）
                for (const Clip* clip : track->clips()) {
                    if (clip->isAudioClip()) continue;

                    double clipStart = static_cast<double>(clip->startTick());
                    double clipEnd = static_cast<double>(clip->endTick());
                    if (trackEndTick < clipStart || trackStartTick >= clipEnd) continue;

                    for (const Note* note : clip->notes()) {
                        double noteStart = clipStart + static_cast<double>(note->startTick());
                        double noteEnd = noteStart + static_cast<double>(note->durationTicks());

                        if (noteStart >= trackStartTick && noteStart < trackEndTick) {
                            int sampleOffset = static_cast<int>((noteStart - trackStartTick) / ticksPerSample);
                            sampleOffset = std::clamp(sampleOffset, 0, framesToProcess - 1);

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
                            activeNotes.push_back(an);
                        }
                    }
                }

                // ソート
                std::sort(trackEvents.begin(), trackEvents.end(),
                          [](const auto& a, const auto& b) {
                              return a.sampleOffset < b.sampleOffset;
                          });

                // プラグイン処理（オーディオクリップがある場合は一時バッファを使いミックス）
                if (hasAudioClips) {
                    if (static_cast<int>(pluginBufL.size()) < framesToProcess) {
                        pluginBufL.resize(framesToProcess);
                        pluginBufR.resize(framesToProcess);
                    }
                    memset(pluginBufL.data(), 0, framesToProcess * sizeof(float));
                    memset(pluginBufR.data(), 0, framesToProcess * sizeof(float));
                    track->pluginInstance()->processAudio(
                        nullptr, nullptr, pluginBufL.data(), pluginBufR.data(),
                        framesToProcess, trackEvents, transport);
                    for (int i = 0; i < framesToProcess; ++i) {
                        trackBufL[i] += pluginBufL[i];
                        trackBufR[i] += pluginBufR[i];
                    }
                } else {
                    track->pluginInstance()->processAudio(
                        nullptr, nullptr, trackBufL.data(), trackBufR.data(),
                        framesToProcess, trackEvents, transport);
                }
            }

            // トラック固有のFXチェーン
            for (VST3PluginInstance* fx : track->fxPlugins()) {
                if (fx && fx->isLoaded() && fx->isAudioPrepared()) {
                    fx->processAudio(
                        trackBufL.data(), trackBufR.data(),
                        trackBufL.data(), trackBufR.data(),
                        framesToProcess, {}, transport);
                }
            }

            // ミックス（ボリューム・パン適用） → 出力先を決定
            double vol = track->volume();
            double pan = track->pan();
            double gainL = vol * std::min(1.0, 1.0 - pan);
            double gainR = vol * std::min(1.0, 1.0 + pan);

            int parentFolderId = track->parentFolderId();
            float* destL = mixBufL.data();
            float* destR = mixBufR.data();
            if (parentFolderId >= 0) {
                auto busIt = folderBuses.find(parentFolderId);
                if (busIt != folderBuses.end()) {
                    destL = busIt->second.bufL.data();
                    destR = busIt->second.bufR.data();
                }
            }

            for (int i = 0; i < framesToProcess; ++i) {
                destL[i] += trackBufL[i] * static_cast<float>(gainL);
                destR[i] += trackBufR[i] * static_cast<float>(gainR);
            }
        }

        // ── Phase 2: フォルダトラックの処理（深いフォルダから順に） ──
        for (int ti : sortedFolderTrackIndices) {
            Track* folder = tracks[ti];
            auto busIt = folderBuses.find(folder->id());
            if (busIt == folderBuses.end()) continue;

            auto& bus = busIt->second;

            // フォルダのFXインサート処理
            for (VST3PluginInstance* fx : folder->fxPlugins()) {
                if (fx && fx->isLoaded() && fx->isAudioPrepared()) {
                    fx->processAudio(
                        bus.bufL.data(), bus.bufR.data(),
                        bus.bufL.data(), bus.bufR.data(),
                        framesToProcess, {}, transport);
                }
            }

            // フォルダのボリューム・パン適用 → 出力先を決定
            double vol = folder->volume();
            double pan = folder->pan();
            double gainL = vol * std::min(1.0, 1.0 - pan);
            double gainR = vol * std::min(1.0, 1.0 + pan);

            int parentFolderId = folder->parentFolderId();
            float* destL = mixBufL.data();
            float* destR = mixBufR.data();
            if (parentFolderId >= 0) {
                auto parentBusIt = folderBuses.find(parentFolderId);
                if (parentBusIt != folderBuses.end()) {
                    destL = parentBusIt->second.bufL.data();
                    destR = parentBusIt->second.bufR.data();
                }
            }

            for (int i = 0; i < framesToProcess; ++i) {
                destL[i] += bus.bufL[i] * static_cast<float>(gainL);
                destR[i] += bus.bufR[i] * static_cast<float>(gainR);
            }
        }

        // WAVデータ書き込み
        for (int i = 0; i < framesToProcess; ++i) {
            float L = std::clamp(mixBufL[i], -1.0f, 1.0f);
            float R = std::clamp(mixBufR[i], -1.0f, 1.0f);

            if (bitDepth == 24) {
                // 24bit PCM
                auto write24 = [&file](float sample) {
                    int32_t val = static_cast<int32_t>(sample * 8388607.0f);
                    val = std::clamp(val, -8388608, 8388607);
                    uint8_t bytes[3];
                    bytes[0] = static_cast<uint8_t>(val & 0xFF);
                    bytes[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
                    bytes[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
                    file.write(reinterpret_cast<char*>(bytes), 3);
                };
                write24(L);
                write24(R);
            } else {
                // 16bit PCM
                auto write16 = [&file](float sample) {
                    int16_t val = static_cast<int16_t>(sample * 32767.0f);
                    file.write(reinterpret_cast<char*>(&val), 2);
                };
                write16(L);
                write16(R);
            }
        }

        framesWritten += framesToProcess;
        currentTick = endTick;

        // 期限切れのアクティブノートを除去
        activeNotes.erase(
            std::remove_if(activeNotes.begin(), activeNotes.end(),
                           [endTick](const ActiveNote& an) { return an.endTick < endTick; }),
            activeNotes.end());

        // 進捗通知
        double progress = static_cast<double>(framesWritten) / totalFrames;
        emit progressChanged(progress);
    }

    // WAVヘッダーのサイズフィールドを更新
    int bytesPerSample = bitDepth / 8;
    uint32_t dataSize = framesWritten * numChannels * bytesPerSample;
    updateWavHeader(file, dataSize);

    file.close();

    QString msg = QString("エクスポート完了: %1\n%2秒, %3Hz, %4bit")
        .arg(filePath)
        .arg(totalSeconds, 0, 'f', 1)
        .arg(static_cast<int>(sampleRate))
        .arg(bitDepth);

    qDebug() << "AudioExporter:" << msg;
    emit exportFinished(true, msg);
    return true;
}

bool AudioExporter::writeWavHeader(QFile& file, int numChannels,
                                    double sampleRate, int bitDepth,
                                    uint32_t numSamples)
{
    int bytesPerSample = bitDepth / 8;
    uint32_t dataSize = numSamples * numChannels * bytesPerSample;
    uint32_t fileSize = 36 + dataSize;
    uint16_t blockAlign = numChannels * bytesPerSample;
    uint32_t byteRate = static_cast<uint32_t>(sampleRate) * blockAlign;
    uint16_t bitsPerSample = static_cast<uint16_t>(bitDepth);
    uint16_t audioFormat = 1; // PCM
    uint16_t channelCount = static_cast<uint16_t>(numChannels);
    uint32_t sr = static_cast<uint32_t>(sampleRate);

    // RIFF ヘッダー
    file.write("RIFF", 4);
    file.write(reinterpret_cast<char*>(&fileSize), 4);
    file.write("WAVE", 4);

    // fmt チャンク
    file.write("fmt ", 4);
    uint32_t fmtSize = 16;
    file.write(reinterpret_cast<char*>(&fmtSize), 4);
    file.write(reinterpret_cast<char*>(&audioFormat), 2);
    file.write(reinterpret_cast<char*>(&channelCount), 2);
    file.write(reinterpret_cast<char*>(&sr), 4);
    file.write(reinterpret_cast<char*>(&byteRate), 4);
    file.write(reinterpret_cast<char*>(&blockAlign), 2);
    file.write(reinterpret_cast<char*>(&bitsPerSample), 2);

    // data チャンク
    file.write("data", 4);
    file.write(reinterpret_cast<char*>(&dataSize), 4);

    return true;
}

bool AudioExporter::updateWavHeader(QFile& file, uint32_t dataSize)
{
    uint32_t fileSize = 36 + dataSize;

    // RIFFサイズを更新
    file.seek(4);
    file.write(reinterpret_cast<char*>(&fileSize), 4);

    // dataサイズを更新
    file.seek(40);
    file.write(reinterpret_cast<char*>(&dataSize), 4);

    return true;
}
