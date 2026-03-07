#pragma once

#include <QSet>
#include <QString>
#include <QVector>

/**
 * @brief ノートのピッチクラスからコード名を検出するユーティリティ
 *
 * MIDI ノートのピッチクラス（0–11）のセットを受け取り、
 * 最も適合するコード名を返す。
 * 1音のみ、またはオクターブ違いを含めて全て同一音名の場合は空文字を返す。
 */
namespace ChordDetector {

// 音名テーブル（♯表記）
static const QString NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"
};

/**
 * @brief コードパターン定義
 */
struct ChordPattern {
    QVector<int> intervals;  ///< ルートからの半音インターバル
    QString suffix;          ///< コード名接尾辞（""＝メジャー等）
};

/**
 * @brief 定義済みコードパターン一覧（具体度の高い順）
 */
inline QVector<ChordPattern> patterns() {
    return {
        // ── 4音コード（より特定的） ──
        {{0, 4, 7, 11}, "maj7"},
        {{0, 4, 7, 10}, "7"},
        {{0, 3, 7, 10}, "m7"},
        {{0, 3, 6, 10}, "m7b5"},
        {{0, 3, 6, 9},  "dim7"},
        {{0, 4, 8, 10}, "aug7"},
        {{0, 3, 7, 11}, "mM7"},
        {{0, 4, 7, 9},  "6"},
        {{0, 3, 7, 9},  "m6"},
        {{0, 5, 7, 10}, "7sus4"},
        {{0, 2, 7, 10}, "7sus2"},
        // ── 3音コード ──
        {{0, 4, 7}, ""},       // メジャー
        {{0, 3, 7}, "m"},      // マイナー
        {{0, 3, 6}, "dim"},    // ディミニッシュ
        {{0, 4, 8}, "aug"},    // オーギュメント
        {{0, 2, 7}, "sus2"},   // サスツー
        {{0, 5, 7}, "sus4"},   // サスフォー
        // ── 2音（パワーコード） ──
        {{0, 7}, "5"},
    };
}

/**
 * @brief ピッチクラスのセットからコード名を検出する
 *
 * @param pitchClasses 0–11 のピッチクラスのセット
 * @param bassPitchClass 最低音のピッチクラス（指定するとルート判定の優先順位とスラッシュ表記に影響）
 * @return コード名（例 "Cmaj7", "Am", "C/E"）。検出不可なら空文字列
 */
inline QString detect(const QSet<int>& pitchClasses, int bassPitchClass = -1) {
    // 1音以下、または全て同じ音名（オクターブ違い含む）→ 表示しない
    if (pitchClasses.size() <= 1) return QString();

    const auto& pats = patterns();

    int bestScore = -9999;
    QString bestChord;
    int bestMatchedRoot = -1;

    // 12個のルート候補それぞれについてマッチング
    for (int root = 0; root < 12; ++root) {
        // ルートが実際のノートに含まれていない場合はスキップ
        if (!pitchClasses.contains(root)) continue;

        // ルートからの相対インターバルを計算
        QSet<int> intervals;
        for (int pc : pitchClasses) {
            intervals.insert((pc - root + 12) % 12);
        }

        // 各コードパターンとのマッチ度を評価
        for (const auto& pattern : pats) {
            bool match = true;
            for (int interval : pattern.intervals) {
                if (!intervals.contains(interval)) {
                    match = false;
                    break;
                }
            }

            if (match) {
                // スコア: パターン構成音数 × 100 − 余分な音数 × 10
                // → より具体的なパターン、余分な音が少ない方を優先
                int score = pattern.intervals.size() * 100
                          - (pitchClasses.size() - pattern.intervals.size()) * 10;

                // ベース音一致ボーナス (+50)
                // これにより、aug などの対称的なコードでベース音をルートとする解釈を優先する
                if (bassPitchClass >= 0 && root == bassPitchClass) {
                    score += 50;
                }

                if (score > bestScore) {
                    bestScore = score;
                    bestMatchedRoot = root;
                    bestChord = NOTE_NAMES[root] + pattern.suffix;
                }
            }
        }
    }

    if (bestMatchedRoot >= 0) {
        // スラッシュコード（オンコード）判定
        // ベスト一致したルートと、実際の最低音が異なる場合に付与
        if (bassPitchClass >= 0 && bestMatchedRoot != bassPitchClass) {
            bestChord += "/" + NOTE_NAMES[bassPitchClass];
        }
        return bestChord;
    }

    return QString();
}

} // namespace ChordDetector
