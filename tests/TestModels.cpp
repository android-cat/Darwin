#include <QtTest/QtTest>

#include <QFileInfo>
#include <QTemporaryDir>

#include "Clip.h"
#include "Note.h"
#include "CCEvent.h"
#include "Project.h"
#include "Track.h"

namespace {

void flushDeferredDeletes()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

QStringList trackNames(const QList<Track*>& tracks)
{
    QStringList names;
    for (const Track* track : tracks) {
        names.append(track->name());
    }
    return names;
}

} // namespace

class ModelTests : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        Track::resetIdCounter();
        flushDeferredDeletes();
    }

    void cleanup()
    {
        flushDeferredDeletes();
    }

    void note_behaviour_and_serialization()
    {
        Note note(60, 120, 480, 90);
        QSignalSpy changedSpy(&note, &Note::changed);

        QCOMPARE(note.pitchName(), QStringLiteral("C4"));
        QCOMPARE(note.endTick(), 600LL);

        note.setPitch(127);
        note.setStartTick(-32);
        note.setDurationTicks(0);
        note.setVelocity(200);

        QCOMPARE(note.pitch(), 127);
        QCOMPARE(note.startTick(), 0LL);
        QCOMPARE(note.durationTicks(), 1LL);
        QCOMPARE(note.velocity(), 127);
        QCOMPARE(changedSpy.count(), 4);

        const QJsonObject json = note.toJson();
        std::unique_ptr<Note> restored(Note::fromJson(json));
        QVERIFY(restored);
        QCOMPARE(restored->pitch(), 127);
        QCOMPARE(restored->startTick(), 0LL);
        QCOMPARE(restored->durationTicks(), 1LL);
        QCOMPARE(restored->velocity(), 127);

        std::unique_ptr<Note> defaults(Note::fromJson(QJsonObject()));
        QVERIFY(defaults);
        QCOMPARE(defaults->pitch(), 60);
        QCOMPARE(defaults->startTick(), 0LL);
        QCOMPARE(defaults->durationTicks(), 480LL);
        QCOMPARE(defaults->velocity(), 100);
    }

    void clip_midi_lifecycle_and_json_roundtrip()
    {
        Clip clip(240, 960);
        QSignalSpy changedSpy(&clip, &Clip::changed);
        QSignalSpy noteAddedSpy(&clip, &Clip::noteAdded);
        QSignalSpy noteRemovedSpy(&clip, &Clip::noteRemoved);

        Note* note = clip.addNote(64, 10, 120, 90);
        QVERIFY(note != nullptr);
        QCOMPARE(clip.notes().size(), 1);
        QCOMPARE(noteAddedSpy.count(), 1);

        note->setVelocity(110);
        QVERIFY(changedSpy.count() >= 2);

        Note* detached = clip.takeNote(note);
        QCOMPARE(detached, note);
        QCOMPARE(clip.notes().size(), 0);
        QCOMPARE(noteRemovedSpy.count(), 1);

        clip.insertNote(detached);
        QCOMPARE(clip.notes().size(), 1);
        QCOMPARE(noteAddedSpy.count(), 2);
        QCOMPARE(detached->parent(), &clip);

        clip.setStartTick(-100);
        clip.setDurationTicks(0);
        QCOMPARE(clip.startTick(), 0LL);
        QCOMPARE(clip.durationTicks(), 1LL);

        const QJsonObject json = clip.toJson();
        std::unique_ptr<Clip> restored(Clip::fromJson(json));
        QVERIFY(restored);
        QVERIFY(restored->isMidiClip());
        QCOMPARE(restored->startTick(), 0LL);
        QCOMPARE(restored->durationTicks(), 1LL);
        QCOMPARE(restored->notes().size(), 1);
        QCOMPARE(restored->notes().first()->pitch(), 64);
        QCOMPARE(restored->notes().first()->velocity(), 110);

        clip.clearNotes();
        QCOMPARE(clip.notes().size(), 0);
        QVERIFY(noteRemovedSpy.count() >= 2);
    }

    void clip_audio_data_and_deferred_restore()
    {
        Clip clip(0, 480);
        QSignalSpy changedSpy(&clip, &Clip::changed);

        const QVector<float> samplesL{0.25f, -0.75f, 0.5f, 0.0f};
        const QVector<float> samplesR{-0.25f, 0.5f, -0.1f, 0.9f};

        clip.setAudioData(samplesL, samplesR, 48000.0, QStringLiteral("/tmp/fake.wav"));

        QVERIFY(clip.isAudioClip());
        QCOMPARE(clip.audioSamplesL(), samplesL);
        QCOMPARE(clip.audioSamplesR(), samplesR);
        QCOMPARE(clip.audioSampleRate(), 48000.0);
        QCOMPARE(clip.audioFilePath(), QStringLiteral("/tmp/fake.wav"));
        QVERIFY(!clip.waveformPreview().isEmpty());
        QCOMPARE(changedSpy.count(), 1);

        const QJsonObject json = clip.toJson();
        std::unique_ptr<Clip> restored(Clip::fromJson(json, nullptr, true));
        QVERIFY(restored);
        QVERIFY(restored->isAudioClip());
        QCOMPARE(restored->audioFilePath(), QStringLiteral("/tmp/fake.wav"));
        QCOMPARE(restored->audioSampleRate(), 48000.0);
        QVERIFY(restored->audioSamplesL().isEmpty());
        QVERIFY(restored->audioSamplesR().isEmpty());
    }

    void track_properties_clip_management_and_roundtrip()
    {
        Track track(QStringLiteral("Pad"));
        QSignalSpy propertySpy(&track, &Track::propertyChanged);
        QSignalSpy clipAddedSpy(&track, &Track::clipAdded);
        QSignalSpy clipRemovedSpy(&track, &Track::clipRemoved);

        track.setInstrumentName(QStringLiteral("Soft Synth"));
        track.setVisible(false);
        track.setMuted(true);
        track.setSolo(true);
        track.setVolume(-0.5);
        track.setPan(2.0);
        track.setTimingOffsetMs(-400.0);
        track.setColor(QColor(QStringLiteral("#123456")));
        track.setIsFolder(true);
        track.setParentFolderId(42);
        track.setFolderExpanded(false);

        QCOMPARE(track.instrumentName(), QStringLiteral("Soft Synth"));
        QCOMPARE(track.isVisible(), false);
        QCOMPARE(track.isMuted(), true);
        QCOMPARE(track.isSolo(), true);
        QCOMPARE(track.volume(), 0.0);
        QCOMPARE(track.pan(), 1.0);
        QCOMPARE(track.timingOffsetMs(), -100.0);
        QCOMPARE(track.color(), QColor(QStringLiteral("#123456")));
        QCOMPARE(track.isFolder(), true);
        QCOMPARE(track.parentFolderId(), 42);
        QCOMPARE(track.isFolderExpanded(), false);

        Clip* midiClip = track.addClip(120, 480);
        QVERIFY(midiClip != nullptr);
        midiClip->addNote(60, 0, 120, 100);

        auto* audioClip = new Clip(960, 240, &track);
        audioClip->setAudioData({0.1f, -0.1f}, {0.2f, -0.2f}, 44100.0,
                                QStringLiteral("/tmp/audio.wav"));
        track.insertClip(audioClip);

        QCOMPARE(track.clips().size(), 2);
        QCOMPARE(clipAddedSpy.count(), 2);
        QCOMPARE(track.clipAt(150), midiClip);
        QCOMPARE(track.clipAt(1000), audioClip);
        QCOMPARE(track.clipAt(5000), nullptr);

        Clip* detached = track.takeClip(audioClip);
        QCOMPARE(detached, audioClip);
        QCOMPARE(track.clips().size(), 1);
        QCOMPARE(clipRemovedSpy.count(), 1);

        track.insertClip(detached);
        QCOMPARE(track.clips().size(), 2);
        QCOMPARE(detached->parent(), &track);

        const QJsonObject json = track.toJson();
        std::unique_ptr<Track> restored(Track::fromJson(json, nullptr, true, true));
        QVERIFY(restored);
        QCOMPARE(restored->id(), track.id());
        QCOMPARE(restored->name(), QStringLiteral("Pad"));
        QCOMPARE(restored->instrumentName(), QStringLiteral("Soft Synth"));
        QCOMPARE(restored->isVisible(), false);
        QCOMPARE(restored->isMuted(), true);
        QCOMPARE(restored->isSolo(), true);
        QCOMPARE(restored->volume(), 0.0);
        QCOMPARE(restored->pan(), 1.0);
        QCOMPARE(restored->timingOffsetMs(), -100.0);
        QCOMPARE(restored->color(), QColor(QStringLiteral("#123456")));
        QCOMPARE(restored->isFolder(), true);
        QCOMPARE(restored->parentFolderId(), 42);
        QCOMPARE(restored->isFolderExpanded(), false);
        QCOMPARE(restored->clips().size(), 2);
        QVERIFY(restored->clips().at(0)->isMidiClip());
        QVERIFY(restored->clips().at(1)->isAudioClip());
        QCOMPARE(restored->clips().at(0)->notes().size(), 1);
        QCOMPARE(restored->clips().at(1)->audioFilePath(), QStringLiteral("/tmp/audio.wav"));

        track.clearClips();
        QCOMPARE(track.clips().size(), 0);
        QVERIFY(clipRemovedSpy.count() >= 3);
        QVERIFY(propertySpy.count() >= 1);
    }

    void ccevent_behaviour_and_serialization()
    {
        // 基本的なCC作成・クランプ動作の確認
        CCEvent ev(11, 480, 100);
        QCOMPARE(ev.ccNumber(), 11);
        QCOMPARE(ev.tick(), 480LL);
        QCOMPARE(ev.value(), 100);

        QSignalSpy changedSpy(&ev, &CCEvent::changed);

        // 値のクランプ（0-127）
        ev.setValue(200);
        QCOMPARE(ev.value(), 127);
        QCOMPARE(changedSpy.count(), 1);

        ev.setValue(-10);
        QCOMPARE(ev.value(), 0);
        QCOMPARE(changedSpy.count(), 2);

        // ティックのクランプ（0以上）
        ev.setTick(-100);
        QCOMPARE(ev.tick(), 0LL);
        QCOMPARE(changedSpy.count(), 3);

        // Pitch Bend（14bit: 0-16383）
        CCEvent bendEv(CCEvent::CC_PITCH_BEND, 960, 8192);
        QCOMPARE(bendEv.value(), 8192);

        bendEv.setValue(20000);
        QCOMPARE(bendEv.value(), 16383);

        bendEv.setValue(-1);
        QCOMPARE(bendEv.value(), 0);

        // Channel Aftertouch (0-127)
        CCEvent atEv(CCEvent::CC_CHANNEL_PRESSURE, 0, 64);
        QCOMPARE(atEv.value(), 64);
        atEv.setValue(200);
        QCOMPARE(atEv.value(), 127);

        // JSONシリアライズ → デシリアライズ
        CCEvent original(1, 240, 80);
        const QJsonObject json = original.toJson();
        std::unique_ptr<CCEvent> restored(CCEvent::fromJson(json));
        QVERIFY(restored);
        QCOMPARE(restored->ccNumber(), 1);
        QCOMPARE(restored->tick(), 240LL);
        QCOMPARE(restored->value(), 80);

        // Pitch Bendのシリアライズ
        CCEvent bendOrig(CCEvent::CC_PITCH_BEND, 120, 12000);
        const QJsonObject bendJson = bendOrig.toJson();
        std::unique_ptr<CCEvent> bendRestored(CCEvent::fromJson(bendJson));
        QVERIFY(bendRestored);
        QCOMPARE(bendRestored->ccNumber(), CCEvent::CC_PITCH_BEND);
        QCOMPARE(bendRestored->value(), 12000);

        // デフォルト値でのデシリアライズ
        std::unique_ptr<CCEvent> defaults(CCEvent::fromJson(QJsonObject()));
        QVERIFY(defaults);
        QCOMPARE(defaults->ccNumber(), 1);
        QCOMPARE(defaults->tick(), 0LL);
        QCOMPARE(defaults->value(), 0);
    }

    void clip_cc_events_lifecycle()
    {
        Clip clip(0, 1920);
        QSignalSpy changedSpy(&clip, &Clip::changed);
        QSignalSpy ccAddedSpy(&clip, &Clip::ccEventAdded);
        QSignalSpy ccRemovedSpy(&clip, &Clip::ccEventRemoved);

        // CCイベント追加
        CCEvent* ev1 = clip.addCCEvent(11, 100, 64);
        QVERIFY(ev1 != nullptr);
        QCOMPARE(ev1->ccNumber(), 11);
        QCOMPARE(ev1->tick(), 100LL);
        QCOMPARE(ev1->value(), 64);
        QCOMPARE(clip.ccEvents().size(), 1);
        QCOMPARE(ccAddedSpy.count(), 1);

        // 異なるCC番号のイベント追加
        CCEvent* ev2 = clip.addCCEvent(1, 200, 100);
        CCEvent* ev3 = clip.addCCEvent(11, 300, 80);
        QCOMPARE(clip.ccEvents().size(), 3);

        // CC番号でフィルタリング
        QList<CCEvent*> cc11 = clip.ccEventsForCC(11);
        QCOMPARE(cc11.size(), 2);
        QList<CCEvent*> cc1 = clip.ccEventsForCC(1);
        QCOMPARE(cc1.size(), 1);
        QList<CCEvent*> ccNone = clip.ccEventsForCC(7);
        QCOMPARE(ccNone.size(), 0);

        // CCイベント削除
        clip.removeCCEvent(ev2);
        QCOMPARE(clip.ccEvents().size(), 2);
        QCOMPARE(ccRemovedSpy.count(), 1);

        // CC番号指定の一括削除
        clip.clearCCEventsForCC(11);
        QCOMPARE(clip.ccEvents().size(), 0);
        flushDeferredDeletes();

        // Pitch Bendイベント
        CCEvent* bend = clip.addCCEvent(CCEvent::CC_PITCH_BEND, 480, 8192);
        QCOMPARE(bend->value(), 8192);

        // 全CCイベント削除
        clip.addCCEvent(11, 0, 127);
        clip.addCCEvent(1, 100, 50);
        QCOMPARE(clip.ccEvents().size(), 3);
        clip.clearCCEvents();
        QCOMPARE(clip.ccEvents().size(), 0);

        // CCイベント変更時のchangedシグナル伝播
        int beforeCount = changedSpy.count();
        CCEvent* ev4 = clip.addCCEvent(11, 0, 64);
        ev4->setValue(100);
        QVERIFY(changedSpy.count() > beforeCount + 1);
    }

    void clip_cc_events_json_roundtrip()
    {
        Clip clip(240, 960);
        clip.addNote(60, 0, 480, 100);
        clip.addCCEvent(11, 0, 64);
        clip.addCCEvent(11, 240, 100);
        clip.addCCEvent(1, 120, 80);
        clip.addCCEvent(CCEvent::CC_PITCH_BEND, 480, 12000);

        const QJsonObject json = clip.toJson();
        std::unique_ptr<Clip> restored(Clip::fromJson(json));
        QVERIFY(restored);
        QVERIFY(restored->isMidiClip());
        QCOMPARE(restored->notes().size(), 1);
        QCOMPARE(restored->ccEvents().size(), 4);

        // CC11が2つ、CC1が1つ、PitchBendが1つ
        QCOMPARE(restored->ccEventsForCC(11).size(), 2);
        QCOMPARE(restored->ccEventsForCC(1).size(), 1);
        QCOMPARE(restored->ccEventsForCC(CCEvent::CC_PITCH_BEND).size(), 1);

        // 値の精度確認
        auto bendEvents = restored->ccEventsForCC(CCEvent::CC_PITCH_BEND);
        QCOMPARE(bendEvents.first()->tick(), 480LL);
        QCOMPARE(bendEvents.first()->value(), 12000);

        // CCイベントなしの場合もJSONラウンドトリップ成功
        Clip emptyClip(0, 480);
        emptyClip.addNote(64, 0, 240, 90);
        const QJsonObject emptyJson = emptyClip.toJson();
        std::unique_ptr<Clip> emptyRestored(Clip::fromJson(emptyJson));
        QVERIFY(emptyRestored);
        QCOMPARE(emptyRestored->ccEvents().size(), 0);
        QCOMPARE(emptyRestored->notes().size(), 1);
    }

    void clip_cc_recording_simulation()
    {
        // PlaybackController の録音パスを模擬:
        // RecordedCCEvent と同じデータを集積し、Clip に一括書き込みする流れをテスト。

        struct RecordedCCEvent {
            int ccNumber;
            qint64 tick;
            int value;
        };

        // 録音データの蓄積をシミュレート
        QVector<RecordedCCEvent> recordedEvents;
        recordedEvents.push_back({11, 0, 64});      // Expression CC
        recordedEvents.push_back({1, 240, 100});     // Modulation CC
        recordedEvents.push_back({CCEvent::CC_PITCH_BEND, 480, 12000}); // Pitch Bend
        recordedEvents.push_back({CCEvent::CC_CHANNEL_PRESSURE, 720, 80}); // Aftertouch

        // finalizeMidiRecordingLocked 相当: クリップ生成 + Event 書き込み
        Clip clip(0, 1920);
        clip.addNote(60, 0, 480, 100); // ノートも同時に存在を想定
        for (const auto& ev : std::as_const(recordedEvents)) {
            clip.addCCEvent(ev.ccNumber, ev.tick, ev.value);
        }

        // 全イベントが記録されていること
        QCOMPARE(clip.ccEvents().size(), 4);
        QCOMPARE(clip.notes().size(), 1);

        // CC番号ごとのフィルタリング
        QCOMPARE(clip.ccEventsForCC(11).size(), 1);
        QCOMPARE(clip.ccEventsForCC(1).size(), 1);
        QCOMPARE(clip.ccEventsForCC(CCEvent::CC_PITCH_BEND).size(), 1);
        QCOMPARE(clip.ccEventsForCC(CCEvent::CC_CHANNEL_PRESSURE).size(), 1);

        // 値の整合性
        auto bends = clip.ccEventsForCC(CCEvent::CC_PITCH_BEND);
        QCOMPARE(bends.first()->tick(), 480LL);
        QCOMPARE(bends.first()->value(), 12000);

        auto ats = clip.ccEventsForCC(CCEvent::CC_CHANNEL_PRESSURE);
        QCOMPARE(ats.first()->tick(), 720LL);
        QCOMPARE(ats.first()->value(), 80);

        // JSON ラウンドトリップ後もCC録音データが保持されること
        const QJsonObject json = clip.toJson();
        std::unique_ptr<Clip> restored(Clip::fromJson(json));
        QVERIFY(restored);
        QCOMPARE(restored->ccEvents().size(), 4);
        QCOMPARE(restored->notes().size(), 1);
        QCOMPARE(restored->ccEventsForCC(CCEvent::CC_PITCH_BEND).size(), 1);
        QCOMPARE(restored->ccEventsForCC(CCEvent::CC_CHANNEL_PRESSURE).size(), 1);
        QCOMPARE(restored->ccEventsForCC(CCEvent::CC_PITCH_BEND).first()->value(), 12000);
    }

    void project_folder_flags_ranges_and_file_roundtrip()
    {
        Project project(QStringLiteral("Song"));
        QSignalSpy trackAddedSpy(&project, &Project::trackAdded);
        QSignalSpy folderSpy(&project, &Project::folderStructureChanged);
        QSignalSpy orderSpy(&project, &Project::trackOrderChanged);
        QSignalSpy exportSpy(&project, &Project::exportRangeChanged);
        QSignalSpy flagsSpy(&project, &Project::flagsChanged);
        QSignalSpy gridSpy(&project, &Project::gridSnapChanged);

        project.setBpm(120.0);
        project.setPlayheadPosition(-100);
        QCOMPARE(project.playheadPosition(), 0LL);

        Track* lead = project.addTrack(QStringLiteral("Lead"));
        Track* folder = project.addFolderTrack(QStringLiteral("Bus"));
        Track* kick = project.addTrack(QStringLiteral("Kick"));
        Track* subFolder = project.addFolderTrack(QStringLiteral("Sub"));
        Track* snare = project.addTrack(QStringLiteral("Snare"));
        Track* ambience = project.addTrack(QStringLiteral("Ambience"));

        kick->addClip(0, 480)->addNote(36, 0, 120, 110);
        snare->addClip(480, 480)->addNote(38, 0, 120, 100);

        project.addTrackToFolder(kick, folder);
        project.addTrackToFolder(subFolder, folder);
        project.addTrackToFolder(snare, subFolder);

        QCOMPARE(trackAddedSpy.count(), 6);
        QCOMPARE(project.folderChildren(folder).size(), 2);
        QCOMPARE(project.folderChildren(subFolder).size(), 1);
        QCOMPARE(project.folderChildren(folder).at(0), kick);
        QCOMPARE(project.folderChildren(folder).at(1), subFolder);
        QCOMPARE(project.folderOf(snare), subFolder);
        QCOMPARE(project.folderDepth(snare), 2);
        QVERIFY(project.isDescendant(snare, folder));

        folder->setFolderExpanded(false);
        QCOMPARE(project.isTrackVisibleInHierarchy(snare), false);
        folder->setFolderExpanded(true);
        QCOMPARE(project.isTrackVisibleInHierarchy(snare), true);

        project.moveTrack(0, 5);
        QCOMPARE(trackNames(project.tracks()),
                 QStringList({QStringLiteral("Bus"), QStringLiteral("Kick"), QStringLiteral("Sub"),
                              QStringLiteral("Snare"), QStringLiteral("Ambience"),
                              QStringLiteral("Lead")}));

        project.moveFolderBlock(folder, 5);
        QCOMPARE(trackNames(project.tracks()),
                 QStringList({QStringLiteral("Ambience"), QStringLiteral("Bus"),
                              QStringLiteral("Kick"), QStringLiteral("Sub"),
                              QStringLiteral("Snare"), QStringLiteral("Lead")}));

        project.removeTrackFromFolder(kick);
        QCOMPARE(kick->parentFolderId(), -1);
        QVERIFY(folderSpy.count() >= 3);
        QCOMPARE(orderSpy.count(), 2);

        QCOMPARE(project.ticksToMs(480), 500LL);
        QCOMPARE(project.msToTicks(500), 480LL);
        QCOMPARE(project.ticksToBeats(960), 2.0);
        QCOMPARE(project.beatsToTicks(1.5), 720LL);

        project.setExportStartBar(1.5);
        project.setExportEndBar(3.0);
        QCOMPARE(project.exportStartTick(), 2880LL);
        QCOMPARE(project.exportEndTick(), 5760LL);
        QCOMPARE(exportSpy.count(), 2);

        project.addFlag(960);
        project.addFlag(0);
        project.addFlag(1920);
        project.addFlag(960);
        QCOMPARE(project.flags(), QList<qint64>({0, 960, 1920}));
        QCOMPARE(project.hasFlag(960), true);
        QCOMPARE(project.nextFlag(0), 960LL);
        QCOMPARE(project.prevFlag(1920), 960LL);
        QCOMPARE(flagsSpy.count(), 3);

        project.setGridSnapEnabled(false);
        project.setGridSnapEnabled(true);
        QCOMPARE(gridSpy.count(), 2);

        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString filePath = tempDir.filePath(QStringLiteral("project.darwin"));

        QVERIFY(project.saveToFile(filePath));
        QVERIFY(QFileInfo::exists(filePath));
        QCOMPARE(project.currentFilePath(), filePath);

        Project restored;
        QVERIFY(restored.loadFromFile(filePath));
        QCOMPARE(restored.currentFilePath(), filePath);
        QCOMPARE(restored.name(), QStringLiteral("Song"));
        QCOMPARE(restored.bpm(), 120.0);
        QCOMPARE(restored.trackCount(), 6);
        QCOMPARE(trackNames(restored.tracks()),
                 QStringList({QStringLiteral("Ambience"), QStringLiteral("Bus"),
                              QStringLiteral("Kick"), QStringLiteral("Sub"),
                              QStringLiteral("Snare"), QStringLiteral("Lead")}));
        QCOMPARE(restored.exportStartBar(), 1.5);
        QCOMPARE(restored.exportEndBar(), 3.0);
        QCOMPARE(restored.flags(), QList<qint64>({0, 960, 1920}));
        QCOMPARE(restored.masterTrack()->name(), QStringLiteral("Master"));

        Track* restoredBus = restored.trackById(folder->id());
        Track* restoredKick = restored.trackById(kick->id());
        Track* restoredSub = restored.trackById(subFolder->id());
        Track* restoredSnare = restored.trackById(snare->id());
        QVERIFY(restoredBus != nullptr);
        QVERIFY(restoredKick != nullptr);
        QVERIFY(restoredSub != nullptr);
        QVERIFY(restoredSnare != nullptr);
        QCOMPARE(restoredKick->parentFolderId(), -1);
        QCOMPARE(restoredSub->parentFolderId(), restoredBus->id());
        QCOMPARE(restoredSnare->parentFolderId(), restoredSub->id());
        QCOMPARE(restored.folderDepth(restoredSnare), 2);
        QCOMPARE(lead != nullptr, true);
    }
};

int runModelTests(int argc, char** argv)
{
    ModelTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "TestModels.moc"
