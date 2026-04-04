#pragma once

#include <QObject>
#include <QMutex>
#include <functional>
#include <memory>

class AudioEngineBackend;

/**
 * @brief バックエンド抽象化を介したオーディオ出力エンジン
 *
 * 呼び出し側へは従来どおりの API を提供しつつ、
 * 実際の OS ネイティブ実装は内部バックエンドへ委譲する。
 */
class AudioEngine : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief オーディオレンダリングコールバック
     * @param outputBuffer インターリーブされたfloat出力バッファ（書き込み先）
     * @param numFrames フレーム数
     * @param numChannels チャンネル数
     * @param sampleRate サンプルレート
     */
    using RenderCallback = std::function<void(float* outputBuffer, int numFrames, int numChannels, double sampleRate)>;

    explicit AudioEngine(QObject* parent = nullptr);
    ~AudioEngine() override;

    /** オーディオデバイスを初期化 */
    bool initialize();

    /** オーディオレンダリングを開始 */
    bool start();

    /** オーディオレンダリングを停止 */
    void stop();

    /** エンジンが実行中か */
    bool isRunning() const;

    /** 現在のサンプルレート */
    double sampleRate() const;

    /** 現在のチャンネル数 */
    int numChannels() const;

    /** バッファサイズ（フレーム単位） */
    int bufferSize() const;

    /** レンダリングコールバックを設定 */
    void setRenderCallback(RenderCallback callback);

signals:
    void errorOccurred(const QString& message);

private:
    void invokeRenderCallback(float* outputBuffer, int numFrames, int numChannels, double sampleRate);
    void reportError(const QString& message);

    std::unique_ptr<AudioEngineBackend> m_backend;
    RenderCallback m_renderCallback;
    mutable QMutex m_mutex;
};
