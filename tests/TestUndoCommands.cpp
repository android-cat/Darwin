#include <QtTest/QtTest>

#include <QUndoStack>

#include "Clip.h"
#include "Note.h"
#include "Project.h"
#include "Track.h"
#include "UndoCommands.h"

namespace {

void flushDeferredDeletes()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

} // namespace

class UndoCommandTests : public QObject
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

    void add_and_remove_note_commands_roundtrip()
    {
        Clip clip(0, 480);
        QUndoStack stack;

        auto* addCommand = new AddNoteCommand(&clip, 60, 0, 120, 100);
        stack.push(addCommand);

        QCOMPARE(stack.count(), 1);
        QCOMPARE(clip.notes().size(), 1);
        QVERIFY(addCommand->createdNote() != nullptr);

        stack.undo();
        QCOMPARE(clip.notes().size(), 0);

        stack.redo();
        QCOMPARE(clip.notes().size(), 1);

        auto* removeCommand = new RemoveNoteCommand(&clip, clip.notes().first());
        stack.push(removeCommand);
        QCOMPARE(clip.notes().size(), 0);

        stack.undo();
        QCOMPARE(clip.notes().size(), 1);

        stack.redo();
        QCOMPARE(clip.notes().size(), 0);
    }

    void note_edit_commands_merge_and_restore_state()
    {
        Note note(60, 0, 120, 90);

        {
            QUndoStack stack;
            stack.push(new MoveNoteCommand(&note, 62, 120));
            stack.push(new MoveNoteCommand(&note, 64, 240));

            QCOMPARE(stack.count(), 1);
            QCOMPARE(note.pitch(), 64);
            QCOMPARE(note.startTick(), 240LL);

            stack.undo();
            QCOMPARE(note.pitch(), 60);
            QCOMPARE(note.startTick(), 0LL);

            stack.redo();
            QCOMPARE(note.pitch(), 64);
            QCOMPARE(note.startTick(), 240LL);
        }

        {
            QUndoStack stack;
            stack.push(new ResizeNoteCommand(&note, 240));
            stack.push(new ResizeNoteCommand(&note, 360));

            QCOMPARE(stack.count(), 1);
            QCOMPARE(note.durationTicks(), 360LL);

            stack.undo();
            QCOMPARE(note.durationTicks(), 120LL);
        }

        {
            QUndoStack stack;
            stack.push(new ChangeVelocityCommand(&note, 100));
            stack.push(new ChangeVelocityCommand(&note, 110));

            QCOMPARE(stack.count(), 1);
            QCOMPARE(note.velocity(), 110);

            stack.undo();
            QCOMPARE(note.velocity(), 90);
        }
    }

    void clip_commands_support_add_remove_and_adopt()
    {
        Track track(QStringLiteral("Track"));
        QUndoStack stack;

        auto* addCommand = new AddClipCommand(&track, 120, 480);
        stack.push(addCommand);

        QCOMPARE(track.clips().size(), 1);
        QVERIFY(addCommand->createdClip() != nullptr);

        stack.undo();
        QCOMPARE(track.clips().size(), 0);

        stack.redo();
        QCOMPARE(track.clips().size(), 1);

        Clip* existing = track.clips().first();
        stack.push(new RemoveClipCommand(&track, existing));
        QCOMPARE(track.clips().size(), 0);

        stack.undo();
        QCOMPARE(track.clips().size(), 1);

        Clip* recordedClip = track.clips().first();
        QUndoStack adoptStack;
        adoptStack.push(new AdoptClipCommand(&track, recordedClip));
        QCOMPARE(track.clips().size(), 1);

        adoptStack.undo();
        QCOMPARE(track.clips().size(), 0);

        adoptStack.redo();
        QCOMPARE(track.clips().size(), 1);
        QCOMPARE(track.clips().first(), recordedClip);
    }

    void track_commands_support_add_remove_and_adopt()
    {
        Project project(QStringLiteral("Undo Song"));
        QUndoStack stack;

        auto* addCommand = new AddTrackCommand(&project, QStringLiteral("Bass"));
        stack.push(addCommand);

        QCOMPARE(project.trackCount(), 1);
        QVERIFY(addCommand->createdTrack() != nullptr);
        QCOMPARE(project.tracks().first()->name(), QStringLiteral("Bass"));

        stack.undo();
        QCOMPARE(project.trackCount(), 0);

        stack.redo();
        QCOMPARE(project.trackCount(), 1);

        Track* createdTrack = project.tracks().first();
        stack.push(new RemoveTrackCommand(&project, createdTrack));
        QCOMPARE(project.trackCount(), 0);

        stack.undo();
        QCOMPARE(project.trackCount(), 1);

        Track* recordedTrack = project.tracks().first();
        QUndoStack adoptStack;
        adoptStack.push(new AdoptTrackCommand(&project, recordedTrack));
        QCOMPARE(project.trackCount(), 1);

        adoptStack.undo();
        QCOMPARE(project.trackCount(), 0);

        adoptStack.redo();
        QCOMPARE(project.trackCount(), 1);
        QCOMPARE(project.tracks().first(), recordedTrack);
    }
};

int runUndoCommandTests(int argc, char** argv)
{
    UndoCommandTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "TestUndoCommands.moc"
