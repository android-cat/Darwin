#include "VST3PluginInstance.h"

// VST3 SDK ヘッダー
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/common/memorystream.h"

#include <QDebug>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

// SDK標準のHostApplicationを使用（IMessage/IAttributeList生成、IPlugInterfaceSupport対応）
static HostApplication g_hostApp;

// コンポーネントハンドラー（プラグインからのパラメータ変更通知などを受け取る）
// VST3PluginInstanceへの逆参照を持ち、restartComponentを転送する
class DarwinComponentHandler : public U::ImplementsNonDestroyable<U::Directly<IComponentHandler>>
{
public:
    VST3PluginInstance* owner = nullptr;

    tresult PLUGIN_API beginEdit(ParamID /*tag*/) override { return kResultTrue; }
    tresult PLUGIN_API performEdit(ParamID tag, ParamValue value) override
    {
        qDebug() << "VST3 Handler: performEdit - ParamID:" << tag << "Value:" << value;
        return kResultTrue;
    }
    tresult PLUGIN_API endEdit(ParamID /*tag*/) override { return kResultTrue; }
    tresult PLUGIN_API restartComponent(int32 flags) override
    {
        qDebug() << "VST3 Handler: restartComponent - Flags:" << flags;
        if (owner) {
            owner->signalRestart(flags);
        }
        return kResultTrue;
    }
};

static DarwinComponentHandler g_componentHandler;

VST3PluginInstance::VST3PluginInstance(QObject* parent)
    : QObject(parent)
    , m_loaded(false)
    , m_plugProvider(nullptr)
{
}

VST3PluginInstance::~VST3PluginInstance()
{
    unload();
}

bool VST3PluginInstance::load(const QString& path)
{
    // 既にロード済みならアンロード
    if (m_loaded) {
        unload();
    }

    std::string errorDesc;
    std::string pathStr = path.toStdString();

    // VST3モジュール（DLL）をロード
    m_module = VST3::Hosting::Module::create(pathStr, errorDesc);
    if (!m_module) {
        qWarning() << "VST3: モジュールロード失敗:" << path << "-" << QString::fromStdString(errorDesc);
        return false;
    }

    auto& factory = m_module->getFactory();

    // インストゥルメントクラスを検索
    VST3::Hosting::ClassInfo targetClassInfo;
    bool found = false;

    for (auto& classInfo : factory.classInfos()) {
        if (classInfo.category() == kVstAudioEffectClass) {
            targetClassInfo = classInfo;
            found = true;
            // サブカテゴリにInstrumentがあればそれを優先
            auto subCats = classInfo.subCategoriesString();
            if (subCats.find("Instrument") != std::string::npos) {
                break;
            }
        }
    }

    if (!found) {
        qWarning() << "VST3: オーディオエフェクトクラスが見つかりません:" << path;
        m_module.reset();
        return false;
    }

    // SDK標準のホストコンテキストを設定（IMessage生成・IPlugInterfaceSupport対応）
    PluginContextFactory::instance().setPluginContext(&g_hostApp);

    // PlugProviderでコンポーネントとコントローラーを初期化
    m_plugProvider = new PlugProvider(factory, targetClassInfo, true);
    if (!m_plugProvider->initialize()) {
        qWarning() << "VST3: Plug-in initialization failed:" << path;
        delete m_plugProvider;
        m_plugProvider = nullptr;
        m_module.reset();
        return false;
    }

    // ハンドラーをセット（逆参照を設定）
    // 注意: getController()/getComponent() は内部で addRef() するので release() が必要
    g_componentHandler.owner = this;
    auto controller = m_plugProvider->getController();
    if (controller) {
        controller->setComponentHandler(&g_componentHandler);
    }

    auto component = m_plugProvider->getComponent();

    // コンポーネントの状態をコントローラーに同期
    // （多くのプラグインで、GUI初期化前にこの同期が必要）
    if (component && controller) {
        MemoryStream stateStream;
        if (component->getState(&stateStream) == kResultTrue) {
            stateStream.seek(0, IBStream::kIBSeekSet, nullptr);
            tresult syncResult = controller->setComponentState(&stateStream);
            if (syncResult == kResultTrue) {
                qDebug() << "VST3: コンポーネントの状態をコントローラーに同期完了";
            } else {
                qDebug() << "VST3: コンポーネント状態の同期に失敗（結果:" << syncResult << "）- 続行";
            }
        } else {
            qDebug() << "VST3: component->getState() に失敗 - 続行";
        }
    }

    // controller の参照を解放（以降は PlugProvider が生存管理）
    if (controller) {
        controller->release();
    }

    // オーディオプロセッサーのセットアップ（GUI表示に必要なプラグインが多い）
    if (component) {
        FUnknownPtr<IAudioProcessor> processor(component);
        if (processor) {
            // プロセッシング設定
            ProcessSetup setup {};
            setup.processMode = Vst::kRealtime;
            setup.symbolicSampleSize = Vst::kSample32;
            setup.maxSamplesPerBlock = 1024;
            setup.sampleRate = 44100.0;

            tresult setupResult = processor->setupProcessing(setup);
            if (setupResult == kResultTrue) {
                qDebug() << "VST3: オーディオプロセッシング設定完了";
            } else {
                qDebug() << "VST3: setupProcessing に失敗（結果:" << setupResult << "）- 続行";
            }

            // 実際のバス数を取得してスピーカーアレンジメントを設定
            // 純粋なインストゥルメント（BBCSymphony等）はオーディオ入力バスが0
            m_numAudioInputBuses = component->getBusCount(kAudio, kInput);
            m_numAudioOutputBuses = component->getBusCount(kAudio, kOutput);
            qDebug() << "VST3: Audio buses - Inputs:" << m_numAudioInputBuses
                     << "Outputs:" << m_numAudioOutputBuses;

            {
                std::vector<SpeakerArrangement> inArr(m_numAudioInputBuses, SpeakerArr::kStereo);
                std::vector<SpeakerArrangement> outArr(m_numAudioOutputBuses, SpeakerArr::kStereo);
                processor->setBusArrangements(
                    inArr.empty() ? nullptr : inArr.data(), m_numAudioInputBuses,
                    outArr.empty() ? nullptr : outArr.data(), m_numAudioOutputBuses);
            }
        }

        // 入出力バスをアクティベート
        int32 numInputBuses = component->getBusCount(kAudio, kInput);
        for (int32 i = 0; i < numInputBuses; ++i) {
            component->activateBus(kAudio, kInput, i, true);
        }
        int32 numOutputBuses = component->getBusCount(kAudio, kOutput);
        for (int32 i = 0; i < numOutputBuses; ++i) {
            component->activateBus(kAudio, kOutput, i, true);
        }

        // イベント（MIDI）バスもアクティベート
        int32 numEventInputBuses = component->getBusCount(kEvent, kInput);
        for (int32 i = 0; i < numEventInputBuses; ++i) {
            component->activateBus(kEvent, kInput, i, true);
        }
        int32 numEventOutputBuses = component->getBusCount(kEvent, kOutput);
        for (int32 i = 0; i < numEventOutputBuses; ++i) {
            component->activateBus(kEvent, kOutput, i, true);
        }

        // コンポーネントをアクティブ化
        component->setActive(true);
        qDebug() << "VST3: コンポーネントをアクティブ化完了";

        // プロセッシングを開始（一部のプラグインでGUI表示に必要）
        FUnknownPtr<IAudioProcessor> proc(component);
        if (proc) {
            proc->setProcessing(true);
        }

        component->release(); // getComponent() の addRef分を解放
    }

    m_pluginName = QString::fromStdString(targetClassInfo.name());
    m_pluginPath = path;
    m_loaded = true;

    qDebug() << "VST3: プラグインロード成功:" << m_pluginName;
    return true;
}

void VST3PluginInstance::unload()
{
    if (m_plugProvider) {
        auto component = m_plugProvider->getComponent();
        auto controller = m_plugProvider->getController();

        // プロセッシングを停止し、コンポーネントを非アクティブ化
        if (component) {
            FUnknownPtr<IAudioProcessor> processor(component);
            if (processor) {
                processor->setProcessing(false);
            }
            component->setActive(false);
            component->release(); // getComponent() の addRef分
        }

        if (controller) {
            controller->release(); // getController() の addRef分
        }

        // ハンドラーの逆参照をクリア
        if (g_componentHandler.owner == this) {
            g_componentHandler.owner = nullptr;
        }

        delete m_plugProvider;
        m_plugProvider = nullptr;
    }

    m_module.reset();
    m_loaded = false;
    m_audioPrepared = false;
    m_pluginName.clear();
    m_pluginPath.clear();
    m_continousTimeSamples = 0;
    m_numAudioInputBuses = 0;
    m_numAudioOutputBuses = 0;
}

void VST3PluginInstance::signalRestart(int flags)
{
    m_pendingRestartFlags.fetch_or(flags);
}

int VST3PluginInstance::consumeRestartFlags()
{
    return m_pendingRestartFlags.exchange(0);
}

// ===== 状態保存/復元 =====

QByteArray VST3PluginInstance::getComponentState() const
{
    if (!m_loaded || !m_plugProvider) {
        return QByteArray();
    }

    auto component = m_plugProvider->getComponent();
    if (!component) {
        return QByteArray();
    }

    MemoryStream stream;
    tresult result = component->getState(&stream);
    component->release();

    if (result != kResultTrue) {
        qWarning() << "VST3: コンポーネント状態の取得に失敗:" << m_pluginName;
        return QByteArray();
    }

    // MemoryStreamからデータを取得
    int64 streamSize = 0;
    stream.seek(0, IBStream::kIBSeekEnd, &streamSize);
    if (streamSize <= 0) {
        return QByteArray();
    }

    QByteArray data(static_cast<int>(streamSize), Qt::Uninitialized);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    int32 bytesRead = 0;
    stream.read(data.data(), static_cast<int32>(streamSize), &bytesRead);

    qDebug() << "VST3: コンポーネント状態を取得:" << m_pluginName << "(" << bytesRead << "bytes)";
    return data;
}

QByteArray VST3PluginInstance::getControllerState() const
{
    if (!m_loaded || !m_plugProvider) {
        return QByteArray();
    }

    auto controller = m_plugProvider->getController();
    if (!controller) {
        return QByteArray();
    }

    // IEditControllerはIComponentを継承しないため、getState()はIEditControllerにある
    MemoryStream stream;
    tresult result = controller->getState(&stream);
    controller->release();

    // 一部プラグインはコントローラー状態の保存をサポートしない
    if (result != kResultTrue) {
        qDebug() << "VST3: コントローラー状態の取得に失敗(非対応の可能性):" << m_pluginName;
        return QByteArray();
    }

    int64 streamSize = 0;
    stream.seek(0, IBStream::kIBSeekEnd, &streamSize);
    if (streamSize <= 0) {
        return QByteArray();
    }

    QByteArray data(static_cast<int>(streamSize), Qt::Uninitialized);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    int32 bytesRead = 0;
    stream.read(data.data(), static_cast<int32>(streamSize), &bytesRead);

    qDebug() << "VST3: コントローラー状態を取得:" << m_pluginName << "(" << bytesRead << "bytes)";
    return data;
}

bool VST3PluginInstance::setComponentState(const QByteArray& data)
{
    if (!m_loaded || !m_plugProvider || data.isEmpty()) {
        return false;
    }

    auto component = m_plugProvider->getComponent();
    if (!component) {
        return false;
    }

    MemoryStream stream;
    int32 bytesWritten = 0;
    stream.write(const_cast<char*>(data.constData()), static_cast<int32>(data.size()), &bytesWritten);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);

    tresult result = component->setState(&stream);

    // コンポーネント状態をコントローラーにも同期
    auto controller = m_plugProvider->getController();
    if (controller) {
        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        controller->setComponentState(&stream);
        controller->release();
    }

    component->release();

    if (result == kResultTrue) {
        qDebug() << "VST3: コンポーネント状態を復元:" << m_pluginName;
    } else {
        qWarning() << "VST3: コンポーネント状態の復元に失敗:" << m_pluginName;
    }
    return result == kResultTrue;
}

bool VST3PluginInstance::setControllerState(const QByteArray& data)
{
    if (!m_loaded || !m_plugProvider || data.isEmpty()) {
        return false;
    }

    auto controller = m_plugProvider->getController();
    if (!controller) {
        return false;
    }

    MemoryStream stream;
    int32 bytesWritten = 0;
    stream.write(const_cast<char*>(data.constData()), static_cast<int32>(data.size()), &bytesWritten);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);

    tresult result = controller->setState(&stream);
    controller->release();

    if (result == kResultTrue) {
        qDebug() << "VST3: コントローラー状態を復元:" << m_pluginName;
    } else {
        qDebug() << "VST3: コントローラー状態の復元に失敗(非対応の可能性):" << m_pluginName;
    }
    return result == kResultTrue;
}

IPlugView* VST3PluginInstance::createView()
{
    if (!m_loaded || !m_plugProvider) {
        return nullptr;
    }

    auto controller = m_plugProvider->getController();
    if (!controller) {
        qWarning() << "VST3: EditController not found for" << m_pluginName;
        return nullptr;
    }
    
    qDebug() << "VST3: Attempting controller->createView(kEditor) for" << m_pluginName;

    // エディタービューを作成
    IPlugView* view = controller->createView(ViewType::kEditor);
    controller->release(); // getController() の addRef分を解放

    if (!view) {
        qWarning() << "VST3: エディタービューの作成に失敗（プラグインにGUIがない可能性）";
        return nullptr;
    }

    return view;
}

// ===== オーディオ処理 =====

// 最小限のIEventList実装（MIDI→VST3イベント変換用）
class DarwinEventList : public IEventList
{
public:
    // IUnknown
    tresult PLUGIN_API queryInterface(const TUID /*_iid*/, void** obj) override
    {
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    // IEventList
    int32 PLUGIN_API getEventCount() override
    {
        return static_cast<int32>(m_events.size());
    }

    tresult PLUGIN_API getEvent(int32 index, Event& e) override
    {
        if (index < 0 || index >= static_cast<int32>(m_events.size()))
            return kInvalidArgument;
        e = m_events[index];
        return kResultTrue;
    }

    tresult PLUGIN_API addEvent(Event& e) override
    {
        m_events.push_back(e);
        return kResultTrue;
    }

    void clear() { m_events.clear(); }

private:
    std::vector<Event> m_events;
};

// 最小限のIParameterChanges実装（空）
class DarwinParameterChanges : public IParameterChanges
{
public:
    tresult PLUGIN_API queryInterface(const TUID /*_iid*/, void** obj) override
    {
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    int32 PLUGIN_API getParameterCount() override { return 0; }
    IParamValueQueue* PLUGIN_API getParameterData(int32 /*index*/) override { return nullptr; }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID& /*id*/, int32& /*index*/) override { return nullptr; }
};

static DarwinEventList s_inputEvents;
static DarwinEventList s_outputEvents;
static DarwinParameterChanges s_inputParamChanges;
static DarwinParameterChanges s_outputParamChanges;

bool VST3PluginInstance::prepareAudio(double sampleRate, int maxBlockSize)
{
    if (!m_loaded || !m_plugProvider) {
        qWarning() << "VST3: prepareAudio - プラグインがロードされていません";
        return false;
    }

    // 同一サンプルレート・ブロックサイズで既に準備済みであれば再初期化をスキップ
    // （Kontakt等は setActive(false)→setupProcessing でサンプルレート変更ダイアログを出すため）
    if (m_audioPrepared && m_currentSampleRate == sampleRate && m_maxBlockSize == maxBlockSize) {
        return true;
    }

    auto component = m_plugProvider->getComponent();
    if (!component) return false;

    FUnknownPtr<IAudioProcessor> processor(component);
    if (!processor) {
        component->release();
        return false;
    }

    // 既存のプロセッシングを停止
    processor->setProcessing(false);
    component->setActive(false);

    // 新しい設定でセットアップ
    ProcessSetup setup {};
    setup.processMode = Vst::kRealtime;
    setup.symbolicSampleSize = Vst::kSample32;
    setup.maxSamplesPerBlock = maxBlockSize;
    setup.sampleRate = sampleRate;

    tresult result = processor->setupProcessing(setup);
    if (result != kResultTrue) {
        qWarning() << "VST3: setupProcessing失敗:" << result;
        component->release();
        return false;
    }

    // 実際のバス数を取得してスピーカーアレンジメントを設定
    m_numAudioInputBuses = component->getBusCount(kAudio, kInput);
    m_numAudioOutputBuses = component->getBusCount(kAudio, kOutput);
    qDebug() << "VST3: prepareAudio - Audio buses - Inputs:" << m_numAudioInputBuses
             << "Outputs:" << m_numAudioOutputBuses;
    {
        std::vector<SpeakerArrangement> inArr(m_numAudioInputBuses, SpeakerArr::kStereo);
        std::vector<SpeakerArrangement> outArr(m_numAudioOutputBuses, SpeakerArr::kStereo);
        processor->setBusArrangements(
            inArr.empty() ? nullptr : inArr.data(), m_numAudioInputBuses,
            outArr.empty() ? nullptr : outArr.data(), m_numAudioOutputBuses);
    }

    // バスアクティベーション
    int32 numInputBuses = component->getBusCount(kAudio, kInput);
    for (int32 i = 0; i < numInputBuses; ++i)
        component->activateBus(kAudio, kInput, i, true);

    int32 numOutputBuses = component->getBusCount(kAudio, kOutput);
    for (int32 i = 0; i < numOutputBuses; ++i)
        component->activateBus(kAudio, kOutput, i, true);

    int32 numEventInputBuses = component->getBusCount(kEvent, kInput);
    for (int32 i = 0; i < numEventInputBuses; ++i)
        component->activateBus(kEvent, kInput, i, true);

    int32 numEventOutputBuses = component->getBusCount(kEvent, kOutput);
    for (int32 i = 0; i < numEventOutputBuses; ++i)
        component->activateBus(kEvent, kOutput, i, true);

    // アクティブ化＆プロセッシング開始
    component->setActive(true);
    processor->setProcessing(true);

    component->release();

    m_currentSampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    m_bufferLeft.resize(maxBlockSize, 0.0f);
    m_bufferRight.resize(maxBlockSize, 0.0f);
    m_audioPrepared = true;

    qDebug() << "VST3: prepareAudio完了 -" << m_pluginName
             << "SampleRate:" << sampleRate << "BlockSize:" << maxBlockSize;
    return true;
}

void VST3PluginInstance::processAudio(float* inputLeft, float* inputRight,
                                       float* outputLeft, float* outputRight, int numFrames,
                                       const std::vector<MidiEvent>& midiEvents,
                                       const TransportInfo& transport)
{
    if (!m_loaded || !m_plugProvider || !m_audioPrepared) {
        // 無音を出力
        memset(outputLeft, 0, numFrames * sizeof(float));
        memset(outputRight, 0, numFrames * sizeof(float));
        return;
    }

    if (m_isBypassed) {
        // バイパス：入力が存在すればそのまま出力し、なければ無音
        if (inputLeft) {
            std::memcpy(outputLeft, inputLeft, numFrames * sizeof(float));
        } else {
            std::memset(outputLeft, 0, numFrames * sizeof(float));
        }
        if (inputRight) {
            std::memcpy(outputRight, inputRight, numFrames * sizeof(float));
        } else {
            std::memset(outputRight, 0, numFrames * sizeof(float));
        }
        return;
    }

    auto component = m_plugProvider->getComponent();
    if (!component) {
        memset(outputLeft, 0, numFrames * sizeof(float));
        memset(outputRight, 0, numFrames * sizeof(float));
        return;
    }

    FUnknownPtr<IAudioProcessor> processor(component);
    if (!processor) {
        component->release();
        memset(outputLeft, 0, numFrames * sizeof(float));
        memset(outputRight, 0, numFrames * sizeof(float));
        return;
    }

    // MIDIイベントをVST3 Eventに変換
    s_inputEvents.clear();
    s_outputEvents.clear();

    for (const auto& me : midiEvents) {
        if (me.type == 2) {
            // CC → VST3 Legacy MIDI CC Event
            Event e {};
            e.busIndex = 0;
            e.sampleOffset = me.sampleOffset;
            e.ppqPosition = 0;
            e.flags = Event::kIsLive;
            e.type = Event::kLegacyMIDICCOutEvent;
            e.midiCCOut.channel = 0;
            e.midiCCOut.controlNumber = me.ccNumber;
            e.midiCCOut.value = me.ccValue;
            e.midiCCOut.value2 = 0;
            s_inputEvents.addEvent(e);
            continue;
        }
        if (me.type == 3) {
            // Pitch Bend → Legacy MIDI CC Event (Pitch Bend = kPitchBend)
            Event e {};
            e.busIndex = 0;
            e.sampleOffset = me.sampleOffset;
            e.ppqPosition = 0;
            e.flags = Event::kIsLive;
            e.type = Event::kLegacyMIDICCOutEvent;
            e.midiCCOut.channel = 0;
            e.midiCCOut.controlNumber = Vst::kPitchBend;
            e.midiCCOut.value = me.bendValue & 0x7F;         // LSB
            e.midiCCOut.value2 = (me.bendValue >> 7) & 0x7F; // MSB
            s_inputEvents.addEvent(e);
            continue;
        }

        Event e {};
        e.busIndex = 0;
        e.sampleOffset = me.sampleOffset;
        e.ppqPosition = 0;
        e.flags = Event::kIsLive;

        if (me.type == 0) {
            // NoteOn
            e.type = Event::kNoteOnEvent;
            e.noteOn.channel = 0;
            e.noteOn.pitch = me.pitch;
            e.noteOn.velocity = me.velocity;
            e.noteOn.length = 0;
            e.noteOn.tuning = 0.0f;
            e.noteOn.noteId = me.pitch; // ノートIDとしてピッチを使用
        } else {
            // NoteOff
            e.type = Event::kNoteOffEvent;
            e.noteOff.channel = 0;
            e.noteOff.pitch = me.pitch;
            e.noteOff.velocity = me.velocity;
            e.noteOff.tuning = 0.0f;
            e.noteOff.noteId = me.pitch;
        }
        s_inputEvents.addEvent(e);
    }

    // === 出力バスの構築（プラグインの実際のバス数に合わせる） ===
    // BBCSymphony等は複数の出力バスを持つ場合がある
    int numOutBuses = (m_numAudioOutputBuses > 0) ? m_numAudioOutputBuses : 1;

    // 補助出力バス用のダミーバッファ（メインバス以外）
    thread_local std::vector<float> auxOutBuf;
    int auxOutNeeded = numFrames * (numOutBuses - 1) * 2;
    if (auxOutNeeded > 0 && static_cast<int>(auxOutBuf.size()) < auxOutNeeded) {
        auxOutBuf.resize(auxOutNeeded, 0.0f);
    }

    // チャンネルポインタ配列: [bus0_L, bus0_R, bus1_L, bus1_R, ...]
    std::vector<float*> outChPtrs(numOutBuses * 2);
    std::vector<AudioBusBuffers> outputBuses(numOutBuses);

    // メイン出力バス（呼び出し元のバッファに直接書き込み）
    outChPtrs[0] = outputLeft;
    outChPtrs[1] = outputRight;
    outputBuses[0].numChannels = 2;
    outputBuses[0].silenceFlags = 0;
    outputBuses[0].channelBuffers32 = &outChPtrs[0];

    // 追加出力バス（ダミーバッファ — プラグインが要求するが使用しない）
    for (int b = 1; b < numOutBuses; ++b) {
        int offset = (b - 1) * 2 * numFrames;
        outChPtrs[b * 2]     = &auxOutBuf[offset];
        outChPtrs[b * 2 + 1] = &auxOutBuf[offset + numFrames];
        memset(outChPtrs[b * 2], 0, numFrames * sizeof(float));
        memset(outChPtrs[b * 2 + 1], 0, numFrames * sizeof(float));
        outputBuses[b].numChannels = 2;
        outputBuses[b].silenceFlags = 0;
        outputBuses[b].channelBuffers32 = &outChPtrs[b * 2];
    }

    // === 入力バスの構築（純粋なインストゥルメントは0バスの場合がある） ===
    int numInBuses = m_numAudioInputBuses;

    thread_local std::vector<float> silenceBuf(8192, 0.0f);
    if (numInBuses > 0) {
        int silenceNeeded = numFrames * numInBuses * 2;
        if (static_cast<int>(silenceBuf.size()) < silenceNeeded) {
            silenceBuf.resize(silenceNeeded, 0.0f);
        }
    }

    std::vector<float*> inChPtrs(numInBuses * 2);
    std::vector<AudioBusBuffers> inputBuses(numInBuses);

    for (int b = 0; b < numInBuses; ++b) {
        if (b == 0 && inputLeft && inputRight) {
            // 最初の入力バスは提供されたバッファを使用
            inChPtrs[0] = inputLeft;
            inChPtrs[1] = inputRight;
            inputBuses[0].silenceFlags = 0;
        } else {
            // 追加の入力バスまたは入力なしの場合は無音バッファ
            int silOff = b * 2 * numFrames;
            inChPtrs[b * 2]     = &silenceBuf[silOff];
            inChPtrs[b * 2 + 1] = &silenceBuf[silOff + numFrames];
            memset(inChPtrs[b * 2], 0, numFrames * sizeof(float));
            memset(inChPtrs[b * 2 + 1], 0, numFrames * sizeof(float));
            inputBuses[b].silenceFlags = 3; // 両チャンネル無音
        }
        inputBuses[b].numChannels = 2;
        inputBuses[b].channelBuffers32 = &inChPtrs[b * 2];
    }

    // === ProcessContext を構築 ===
    ProcessContext processContext {};
    
    // ティックから秒・サンプル・拍位置を計算
    double ticksPerSecond = transport.bpm * transport.ticksPerBeat / 60.0;
    double positionInSeconds = (ticksPerSecond > 0.0) 
        ? transport.positionInTicks / ticksPerSecond 
        : 0.0;
    double positionInSamples = positionInSeconds * transport.sampleRate;
    double positionInQuarterNotes = transport.positionInTicks / static_cast<double>(transport.ticksPerBeat);

    // 小節の先頭位置を計算（quarter notes per bar）
    double quarterNotesPerBar = transport.timeSigNumerator * (4.0 / transport.timeSigDenominator);
    double barPositionMusic = 0.0;
    if (quarterNotesPerBar > 0.0) {
        barPositionMusic = std::floor(positionInQuarterNotes / quarterNotesPerBar) * quarterNotesPerBar;
    }

    processContext.sampleRate = transport.sampleRate;
    processContext.projectTimeSamples = static_cast<Steinberg::int64>(positionInSamples);
    processContext.projectTimeMusic = positionInQuarterNotes;
    processContext.barPositionMusic = barPositionMusic;
    processContext.tempo = transport.bpm;
    processContext.timeSigNumerator = transport.timeSigNumerator;
    processContext.timeSigDenominator = transport.timeSigDenominator;
    processContext.continousTimeSamples = m_continousTimeSamples;

    // フラグ設定
    processContext.state = ProcessContext::kTempoValid
                         | ProcessContext::kTimeSigValid
                         | ProcessContext::kProjectTimeMusicValid
                         | ProcessContext::kBarPositionValid
                         | ProcessContext::kContTimeValid;
    if (transport.isPlaying) {
        processContext.state |= ProcessContext::kPlaying;
    }

    // 連続再生サンプル数を更新
    m_continousTimeSamples += numFrames;

    // ProcessDataを構築
    ProcessData processData {};
    processData.processMode = Vst::kRealtime;
    processData.symbolicSampleSize = Vst::kSample32;
    processData.numSamples = numFrames;
    processData.numInputs = numInBuses;
    processData.inputs = inputBuses.empty() ? nullptr : inputBuses.data();
    processData.numOutputs = numOutBuses;
    processData.outputs = outputBuses.data();
    processData.inputEvents = &s_inputEvents;
    processData.outputEvents = &s_outputEvents;
    processData.inputParameterChanges = &s_inputParamChanges;
    processData.outputParameterChanges = &s_outputParamChanges;
    processData.processContext = &processContext;

    // プロセス実行
    tresult result = processor->process(processData);
    if (result != kResultTrue) {
        // エラー時は無音
        memset(outputLeft, 0, numFrames * sizeof(float));
        memset(outputRight, 0, numFrames * sizeof(float));
    }

    component->release();
}
