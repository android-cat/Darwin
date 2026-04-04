#pragma once

/**
 * @brief オーディオファイルの読み込みユーティリティ
 *
 * WAVはネイティブ解析、MP3/M4AはOSネイティブのデコーダ経由でデコードする。
 * デコード済みPCMデータと波形プレビューを返却する。
 */

#include <QString>
#include <QVector>

/**
 * @brief デコード済みオーディオデータ
 */
struct AudioFileData {
    QVector<float> samplesL;        ///< 左チャンネルPCMデータ（-1.0〜1.0）
    QVector<float> samplesR;        ///< 右チャンネルPCMデータ（-1.0〜1.0）
    double sampleRate = 44100.0;    ///< サンプルレート
    int channels = 0;               ///< チャンネル数
    QVector<float> waveformPreview; ///< 波形プレビュー（表示用ピーク値配列）
    bool valid = false;             ///< 読み込み成功フラグ
    QString errorMessage;           ///< エラー発生時のメッセージ
};

class AudioFileReader {
public:
    /**
     * @brief オーディオファイルを読み込みデコードする
     * @param filePath ファイルパス（.wav / .mp3 / .m4a）
     * @return デコード済みオーディオデータ
     */
    static AudioFileData readFile(const QString& filePath);

    /**
     * @brief PCMデータから波形プレビューを生成する
     * @param samplesL 左チャンネルデータ
     * @param samplesR 右チャンネルデータ
     * @param previewWidth プレビューポイント数
     * @return ダウンサンプルされたピーク値配列
     */
    static QVector<float> generateWaveformPreview(
        const QVector<float>& samplesL,
        const QVector<float>& samplesR,
        int previewWidth = 2048);

    /**
     * @brief 対応する拡張子かどうかを判定する
     * @param filePath ファイルパス
     * @return 対応ファイルならtrue
     */
    static bool isSupportedAudioFile(const QString& filePath);

private:
    /** WAVファイルをネイティブ解析 */
    static AudioFileData readWav(const QString& filePath);

    /** OSネイティブデコーダで圧縮音声をデコード（MP3, M4A等） */
    static AudioFileData readCompressedAudio(const QString& filePath);
};
