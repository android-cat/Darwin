#pragma once

#include <QObject>
#include <QList>
#include <QVector>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>

class Note;
class CCEvent;

/**
 * @brief クリップの種類
 */
enum class ClipType {
    Midi,   ///< MIDIクリップ（ノートデータ）
    Audio   ///< オーディオクリップ（PCMデータ）
};

/**
 * @brief クリップ（MIDI/オーディオリージョン）を表すクラス
 */
class Clip : public QObject
{
    Q_OBJECT

public:
    explicit Clip(qint64 startTick, qint64 durationTicks, QObject* parent = nullptr);
    ~Clip() override;

    // Getters
    int id() const { return m_id; }
    qint64 startTick() const { return m_startTick; }
    qint64 durationTicks() const { return m_durationTicks; }
    qint64 endTick() const { return m_startTick + m_durationTicks; }
    const QList<Note*>& notes() const { return m_notes; }

    // クリップ種別
    ClipType clipType() const { return m_clipType; }
    bool isAudioClip() const { return m_clipType == ClipType::Audio; }
    bool isMidiClip() const { return m_clipType == ClipType::Midi; }

    // Setters
    void setStartTick(qint64 startTick);
    void setDurationTicks(qint64 durationTicks);

    // Note management (MIDIクリップ用)
    Note* addNote(int pitch, qint64 startTick, qint64 durationTicks, int velocity = 100);
    void removeNote(Note* note);
    Note* takeNote(Note* note);      // 追加
    void insertNote(Note* note);     // 追加
    void clearNotes();

    // CCイベント管理（エクスプレッション / モジュレーション / Pitch Bend 等）
    const QList<CCEvent*>& ccEvents() const { return m_ccEvents; }
    QList<CCEvent*> ccEventsForCC(int ccNumber) const;
    CCEvent* addCCEvent(int ccNumber, qint64 tick, int value);
    void removeCCEvent(CCEvent* event);
    void clearCCEvents();
    void clearCCEventsForCC(int ccNumber);

    // ===== オーディオクリップ関連 =====

    /** オーディオファイルを読み込んでオーディオクリップとして設定する */
    bool loadAudioFile(const QString& filePath, double projectSampleRate = 44100.0);

    /** オーディオファイルパス */
    QString audioFilePath() const { return m_audioFilePath; }

    /** デコード済み左チャンネルPCMデータ */
    const QVector<float>& audioSamplesL() const { return m_audioSamplesL; }
    /** デコード済み右チャンネルPCMデータ */
    const QVector<float>& audioSamplesR() const { return m_audioSamplesR; }

    /** オーディオのサンプルレート */
    double audioSampleRate() const { return m_audioSampleRate; }

    /** 波形プレビュー（表示用にダウンサンプル済みピーク値配列） */
    const QVector<float>& waveformPreview() const { return m_waveformPreview; }

    /** オーディオデータを直接設定する（リサンプル済みデータ等） */
    void setAudioData(const QVector<float>& samplesL, const QVector<float>& samplesR,
                      double sampleRate, const QString& filePath = QString(),
                      const QVector<float>& waveformPreview = {});

    // シリアライズ
    QJsonObject toJson() const;
    static Clip* fromJson(const QJsonObject& json, QObject* parent = nullptr,
                          bool deferAudioLoad = false);

signals:
    void changed();
    void noteAdded(Note* note);
    void noteRemoved(Note* note);
    void ccEventAdded(CCEvent* event);
    void ccEventRemoved(CCEvent* event);

private:
    /** 波形プレビューを再生成する */
    void regenerateWaveformPreview();

    static int s_nextId;
    int m_id;
    qint64 m_startTick;
    qint64 m_durationTicks;
    QList<Note*> m_notes;
    QList<CCEvent*> m_ccEvents;  ///< CCオートメーションイベント

    // オーディオクリップ用データ
    ClipType m_clipType = ClipType::Midi;
    QString m_audioFilePath;             ///< 元オーディオファイルパス
    QVector<float> m_audioSamplesL;      ///< 左チャンネルPCMデータ
    QVector<float> m_audioSamplesR;      ///< 右チャンネルPCMデータ
    double m_audioSampleRate = 44100.0;  ///< サンプルレート
    QVector<float> m_waveformPreview;    ///< 表示用波形プレビュー
};
