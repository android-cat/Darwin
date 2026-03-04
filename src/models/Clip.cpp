#include "Clip.h"
#include "Note.h"
#include "common/AudioFileReader.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDebug>
#include <cmath>
#include <QMutexLocker>
#include "common/ModelAccessLock.h"

int Clip::s_nextId = 1;

Clip::Clip(qint64 startTick, qint64 durationTicks, QObject* parent)
    : QObject(parent)
    , m_id(s_nextId++)
    , m_startTick(startTick)
    , m_durationTicks(durationTicks)
{
}

Clip::~Clip()
{
    clearNotes();
}

void Clip::setStartTick(qint64 startTick)
{
    if (m_startTick != startTick) {
        m_startTick = qMax(0LL, startTick);
        emit changed();
    }
}

void Clip::setDurationTicks(qint64 durationTicks)
{
    if (m_durationTicks != durationTicks) {
        m_durationTicks = qMax(1LL, durationTicks);
        emit changed();
    }
}

Note* Clip::addNote(int pitch, qint64 startTick, qint64 durationTicks, int velocity)
{
    QMutexLocker<QRecursiveMutex> locker(&Darwin::modelAccessMutex());

    Note* note = new Note(pitch, startTick, durationTicks, velocity, this);
    m_notes.append(note);
    
    connect(note, &Note::changed, this, &Clip::changed);
    
    emit noteAdded(note);
    emit changed();
    
    return note;
}

void Clip::removeNote(Note* note)
{
    QMutexLocker<QRecursiveMutex> locker(&Darwin::modelAccessMutex());

    if (m_notes.removeOne(note)) {
        emit noteRemoved(note);
        emit changed();
        note->deleteLater();
    }
}

Note* Clip::takeNote(Note* note)
{
    QMutexLocker<QRecursiveMutex> locker(&Darwin::modelAccessMutex());

    if (m_notes.removeOne(note)) {
        emit noteRemoved(note);
        emit changed();
        return note;
    }
    return nullptr;
}

void Clip::insertNote(Note* note)
{
    QMutexLocker<QRecursiveMutex> locker(&Darwin::modelAccessMutex());

    if (!note) return;
    note->setParent(this);
    m_notes.append(note);
    connect(note, &Note::changed, this, &Clip::changed);
    emit noteAdded(note);
    emit changed();
}

void Clip::clearNotes()
{
    QMutexLocker<QRecursiveMutex> locker(&Darwin::modelAccessMutex());

    for (Note* note : m_notes) {
        emit noteRemoved(note);
        note->deleteLater();
    }
    m_notes.clear();
    emit changed();
}

QJsonObject Clip::toJson() const
{
    QJsonObject json;
    json["startTick"] = m_startTick;
    json["durationTicks"] = m_durationTicks;
    json["clipType"] = (m_clipType == ClipType::Audio) ? "audio" : "midi";

    if (m_clipType == ClipType::Audio) {
        // オーディオクリップ: ファイルパスとサンプルレートを保存
        json["audioFilePath"] = m_audioFilePath;
        json["audioSampleRate"] = m_audioSampleRate;
    } else {
        // MIDIクリップ: ノートデータを保存
        QJsonArray notesArray;
        for (const Note* note : m_notes) {
            notesArray.append(note->toJson());
        }
        json["notes"] = notesArray;
    }

    return json;
}

Clip* Clip::fromJson(const QJsonObject& json, QObject* parent)
{
    QElapsedTimer uiYieldClock;
    uiYieldClock.start();

    qint64 startTick = static_cast<qint64>(json["startTick"].toDouble(0));
    qint64 durationTicks = static_cast<qint64>(json["durationTicks"].toDouble(480));

    auto* clip = new Clip(startTick, durationTicks, parent);

    // クリップ種別の判定
    QString clipTypeStr = json["clipType"].toString("midi");
    if (clipTypeStr == "audio") {
        // オーディオクリップの復元
        clip->m_clipType = ClipType::Audio;
        QString audioPath = json["audioFilePath"].toString();
        double savedSampleRate = json["audioSampleRate"].toDouble(44100.0);

        if (!audioPath.isEmpty()) {
            // オーディオファイルを再読み込み
            if (!clip->loadAudioFile(audioPath, savedSampleRate)) {
                qWarning() << "オーディオファイルの再読み込みに失敗:" << audioPath;
                // パスだけ保持しておく（データは無い状態）
                clip->m_audioFilePath = audioPath;
                clip->m_audioSampleRate = savedSampleRate;
            }
        }
    } else {
        // MIDIクリップの復元
        QJsonArray notesArray = json["notes"].toArray();
        int noteIndex = 0;
        for (const QJsonValue& val : notesArray) {
            QJsonObject noteJson = val.toObject();
            Note* note = Note::fromJson(noteJson, clip);
            clip->m_notes.append(note);
            QObject::connect(note, &Note::changed, clip, &Clip::changed);

            ++noteIndex;
            if ((noteIndex % 256) == 0 || uiYieldClock.elapsed() >= 8) {
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 2);
                uiYieldClock.restart();
            }
        }
    }

    return clip;
}

// ===== オーディオクリップ関連 =====

bool Clip::loadAudioFile(const QString& filePath, double projectSampleRate)
{
    AudioFileData data = AudioFileReader::readFile(filePath);
    if (!data.valid) {
        qWarning() << "オーディオファイル読み込みエラー:" << data.errorMessage;
        return false;
    }

    m_clipType = ClipType::Audio;
    m_audioFilePath = filePath;
    m_audioSampleRate = data.sampleRate;
    m_audioSamplesL = data.samplesL;
    m_audioSamplesR = data.samplesR;
    m_waveformPreview = data.waveformPreview;

    qDebug() << QStringLiteral("オーディオクリップ作成: %1 (%2 samples, %3 Hz)")
                .arg(QFileInfo(filePath).fileName())
                .arg(m_audioSamplesL.size())
                .arg(m_audioSampleRate);

    emit changed();
    return true;
}

void Clip::setAudioData(const QVector<float>& samplesL, const QVector<float>& samplesR,
                         double sampleRate, const QString& filePath)
{
    m_clipType = ClipType::Audio;
    m_audioSamplesL = samplesL;
    m_audioSamplesR = samplesR;
    m_audioSampleRate = sampleRate;
    m_audioFilePath = filePath;
    regenerateWaveformPreview();
    emit changed();
}

void Clip::regenerateWaveformPreview()
{
    m_waveformPreview = AudioFileReader::generateWaveformPreview(
        m_audioSamplesL, m_audioSamplesR, 2048);
}
