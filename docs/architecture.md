# Darwin DAW - アーキテクチャ設計書

## 1. 概要

Darwin DAWは、Qt6/C++で構築されたデジタルオーディオワークステーション（DAW）です。
モダンでミニマルなUIデザインと、直感的な操作性を重視しています。
VST3プラグインのホスティング、WindowsではWASAPI、macOSではCoreAudio / AudioUnit経由のリアルタイムオーディオ出力に対応しています。
オーディオファイル（WAV/MP3/M4A）のインポート・波形表示・再生にも対応しています。

## 2. レイヤー構成

```
┌─────────────────────────────────────────────────────────┐
│                    UI Layer (Views)                      │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐   │
│  │SourceView│ │ComposeView│ │ MixView  │ │MainWindow│   │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘   │
│  ┌────────────────┐ ┌──────────────────┐                │
│  │ArrangementView │ │  PianoRollView   │                │
│  └────────────────┘ └──────────────────┘                │
├─────────────────────────────────────────────────────────┤
│                   Widget Layer                           │
│  ┌────────────────┐ ┌────────────┐ ┌──────────────┐    │
│  │MixerChannelWidget│ │KnobWidget │ │VelocityLane │    │
│  └────────────────┘ └────────────┘ └──────────────┘    │
│  ┌──────────────────┐ ┌──────────────────┐             │
│  │ArrangementGridW. │ │PianoRollGridW.   │             │
│  └──────────────────┘ └──────────────────┘             │
│  ┌──────────────────┐                                   │
│  │PluginEditorWidget│                                   │
│  └──────────────────┘                                   │
├─────────────────────────────────────────────────────────┤
│                  Controller Layer                        │
│  ┌────────────────────┐ ┌──────────────────┐            │
│  │PlaybackController  │ │  AudioExporter   │            │
│  └────────────────────┘ └──────────────────┘            │
├─────────────────────────────────────────────────────────┤
│                  Command Layer (Undo/Redo)               │
│  ┌──────────────────────────────────────────────────┐   │
│  │ UndoCommands (Note/Clip/Track Actions)           │   │
│  └──────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────┤
│                   Audio Layer                            │
│  ┌────────────────┐                                     │
│  │  AudioEngine   │ ← OSネイティブ音声API               │
│  └────────────────┘                                     │
├─────────────────────────────────────────────────────────┤
│                  Plugin Layer                            │
│  ┌──────────────────┐ ┌──────────────┐                  │
│  │VST3PluginInstance│ │ VST3Scanner  │                  │
│  └──────────────────┘ └──────────────┘                  │
├─────────────────────────────────────────────────────────┤
│                    Model Layer                           │
│  ┌─────────┐ ┌───────┐ ┌──────┐ ┌──────┐              │
│  │ Project │ │ Track │ │ Clip │ │ Note │              │
│  └─────────┘ └───────┘ └──────┘ └──────┘              │
├─────────────────────────────────────────────────────────┤
│                    Common Layer                          │
│  ┌──────────────────────────────────────────────────┐   │
│  │ Constants, ThemeManager, ChordDetector,          │   │
│  │ MidiFileParser, AudioFileReader, etc.            │   │
│  └──────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

## 3. 主要コンポーネント

### 3.1 UI Layer

| コンポーネント | 責務 |
|---------------|------|
| `MainWindow` | アプリケーションのメインウィンドウ。ヘッダー、ナビゲーション、ビュー切替、トランスポート（再生・巻き戻し・フラッグスキップ）を管理 |
| `SourceView` | VST3インストゥルメント一覧表示、スキャン設定、プラグインGUI表示。ScanSpinnerWidget・PluginLoadOverlayの実装は個別ファイルに分離 |
| `ComposeView` | ArrangementView + PianoRollView の分割ビュー。プラグインエディターも統合 |
| `MixView` | ミキサーコンソール。フォルダトラックの展開/折りたたみに対応し、子トラックをフォルダカラーの薄い角丸コンテナで囲んで表示。ネストされたフォルダも再帰的に処理。折りたたみ時は子トラック数バッジを表示 |
| `ArrangementView` | トラックヘッダー + ArrangementGridWidgetのスクロール管理。長押しD&Dによるトラック/フォルダの並び替え、フォルダへのドロップ格納に対応。実装は機能単位で分割: Headers/DragDrop |
| `PianoRollView` | ピアノキーボード + PianoRollGridWidget + VelocityLaneWidgetのスクロール管理 |

### 3.2 Widget Layer

| コンポーネント | 責務 |
|---------------|------|
| `ArrangementGridWidget` | タイムライングリッドの描画、クリップ操作（選択・移動・リサイズ・作成・長押し分割）。クリップ上で長押し（500ms）すると斬撃アニメーション付きで分割。分割時にノートがまたがっている場合は双方のクリップに分割配置。MIDIファイルおよびオーディオファイル（WAV/MP3/M4A）のD&Dインポートに対応。オーディオクリップは波形プレビューを描画。実装は機能単位で分割: Painting/Input/DragDrop/Animation |
| `TimelineWidget` | 小節番号・エクスポート範囲ハンドル・フラッグ（マーカー）の描画。長押しでフラッグ設置、右クリックでフラッグ削除 |
| `PianoRollGridWidget` | ピアノロールグリッドの描画、ノート操作、ゴーストノート、ズーム。実装は機能単位で分割: Painting/Input |
| `VelocityLaneWidget` | ノートベロシティの表示・編集 |
| `MixerChannelWidget` | 単一ミキサーチャンネルのUI。フォルダトラックの場合はシェブロン展開/折りたたみボタン・フォルダカラーアクセント付き |
| `KnobWidget` | 回転ノブUI |
| `PluginEditorWidget` | VST3プラグインGUI埋め込み。Windows ではダイレクト＋ビットマップキャプチャ、macOS では NSView 直結を担当し、親UIとのイベント競合時はプラグインビューを優先する |

### 3.3 Controller Layer

| コンポーネント | 責務 |
|---------------|------|
| `PlaybackController` | 再生/停止、再生位置管理、BPM制御、AudioEngineとの連携、MIDIイベント収集・送信。オーディオクリップの直接PCM再生にも対応（tick→サンプルインデックスの変換、線形補間）。フォルダバス（2フェーズ処理）による階層的オーディオルーティング：Phase1で非フォルダトラック（MIDI+オーディオクリップ）を処理しフォルダバスへ集約、Phase2で深いフォルダから順にFX適用・ボリューム/パン反映して親バスまたはマスターへルーティング |
| `AudioExporter` | プロジェクト全体のオーディオをWAVファイルとしてオフラインレンダリング（エクスポート）。PlaybackControllerと同様のフォルダバス2フェーズ処理に対応。オーディオクリップのPCMデータも含めてミックスダウン |

### 3.4 Command Layer

| コンポーネント | 責務 |
|---------------|------|
| `UndoCommands` | 各種操作（ノート追加・移動、トラック削除等）を `QUndoCommand` としてカプセル化。Undo/Redo機能を提供 |

### 3.5 Audio Layer

| コンポーネント | 責務 |
|---------------|------|
| `AudioEngine` | リアルタイムオーディオ出力の公開窓口。内部では OS 別バックエンドへ委譲し、Windows では WASAPI、macOS では AudioUnit を利用する |
| `AudioInputCapture` | リアルタイム録音入力の公開窓口。内部では OS 別バックエンドへ委譲し、Windows では WASAPI Capture、macOS では HAL Output AudioUnit を利用する |

### 3.6 Plugin Layer

| コンポーネント | 責務 |
|---------------|------|
| `VST3PluginInstance` | VST3プラグインのライフサイクル管理（DLLロード→コンポーネント初期化→GUI取得→オーディオ処理） |
| `VST3Scanner` | ファイルシステムからVST3プラグインを再帰スキャン、カテゴリ検出。macOS では `moduleinfo.json` / `Info.plist` を優先し、不足時のみ別プロセス probe を使う |
| `VST3MetadataProbe` | macOS 専用の補助コンポーネント。Hosting Module を helper process 側で実行し、JSON で metadata を返す |

### 3.7 Model Layer

| コンポーネント | 責務 |
|---------------|------|
| `Project` | プロジェクト全体のルートモデル。BPM、再生位置、ティック⇔ミリ秒変換、フォルダ管理（addTrackToFolder/removeTrackFromFolder/moveFolderBlock）、フラッグ（マーカー）管理（addFlag/removeFlag/nextFlag/prevFlag） |
| `Track` | 単一トラックのデータ。ボリューム、パン、ミュート/ソロ、カラー、プラグインインスタンス保持。フォルダトラック機能（isFolder/parentFolderId/folderExpanded）対応 |
| `Clip` | 単一クリップのデータ。MIDIクリップ（ノートデータ保持）またはオーディオクリップ（PCMデータ・波形プレビュー保持）の2種別に対応。`ClipType`列挙体で区別 |
| `Note` | 単一MIDIノートのデータ |

### 3.8 Common Layer

| コンポーネント | 責務 |
|---------------|------|
| `ThemeManager` | シングルトン。アプリ全体のテーマ（Light/Dark）状態の保持、OS設定の読み取り、UI色定数の提供と切り替え通知 |
| `FontManager` | アプリ全体のフォント定義元。UI/Mono フォントの family・`QFont`・CSS を提供し、起動時に旧来フォント名のエイリアスも適用 |
| `Constants` | タイミング（Ticks）、レイアウト等のプロジェクト共通定数 |
| `ChordDetector` | ノート構成からコード名を判定 |
| `MidiFileParser` | 標準MIDIファイルの読み込み・パース |
| `AudioFileReader` | オーディオファイル（WAV/MP3/M4A）のデコード。WAVはネイティブ解析、MP3/M4AはOSネイティブデコーダ（Windows: Media Foundation / macOS: ExtAudioFile）を利用。波形プレビュー生成機能も提供 |
| `BurstAnimationHelper` | UI演出用のアニメーション支援 |

## 4. データの所有関係

```
Project (1)
  ├── Track (*)
  │     ├── Clip (*)
  │     │     ├── [MIDIクリップ] Note (*)
  │     │     └── [オーディオクリップ] PCMデータ (samplesL/R), 波形プレビュー
  │     └── VST3PluginInstance (0..1)
  └── Flag (*) ← ティック位置のリスト（マーカー）
```

- `Project`は複数の`Track`を所有
- `Project`は複数のフラッグ（ティック位置のリスト）を所有・シリアライズ対象
- `Track`は複数の`Clip`と、オプションで1つの`VST3PluginInstance`を所有
- `Clip`はMIDIクリップの場合は複数の`Note`を所有、オーディオクリップの場合はPCMサンプルデータ（L/R）と波形プレビューを保持

## 5. シグナル/スロット設計方針

### 5.1 データ変更通知

モデルが変更された場合、シグナルを発行してビューを更新：

```cpp
// Model → View
Project::trackAdded(Track*)
Track::clipAdded(Clip*)
Clip::noteAdded(Note*)
Note::changed()
```

### 5.2 ユーザー操作

ビューからのユーザー操作はコントローラーまたはモデルに直接：

```cpp
// View → Controller → Model (再生制御)
MainWindow::onPlayButtonClicked()
  → PlaybackController::togglePlayPause()

// View → Model (ノート編集)
PianoRollGridWidget::mouseReleaseEvent()
  → Clip::addNote(...)
    → Clip::noteAdded(Note*)  // シグナル発行
```

### 5.3 再生・オーディオフロー

```cpp
// PlaybackController → AudioEngine → VST3PluginInstance
PlaybackController::play()
  → AudioEngine::start()
    → platform render callback (WASAPI / AudioUnit)
      → PlaybackController::audioRenderCallback()
        → VST3PluginInstance::processAudio(output, midi)
```

## 6. ファイル構成

```
Darwin/
├── src/
│   ├── main.cpp
│   ├── MainWindow.h/.cpp
│   ├── Resources.qrc
│   ├── Darwin.rc (Windowsのみ)
│   ├── audio/
│   │   ├── AudioEngine.h/.cpp
│   │   ├── AudioEngineBackend.h/.cpp
│   │   ├── AudioInputCapture.h/.cpp
│   │   ├── AudioInputCaptureBackend.h/.cpp
│   │   └── AudioExporter.h/.cpp
│   ├── commands/
│   │   └── UndoCommands.h/.cpp
│   ├── common/
│   │   ├── AudioFileReader.h/.cpp
│   │   ├── BurstAnimationHelper.h
│   │   ├── ChordDetector.h
│   │   ├── Constants.h
│   │   ├── FadeHelper.h
│   │   ├── MidiFileParser.h
│   │   └── ThemeManager.h/.cpp
│   ├── controllers/
│   │   └── PlaybackController.h/.cpp
│   ├── models/
│   │   ├── Project.h/.cpp
│   │   ├── Track.h/.cpp
│   │   ├── Clip.h/.cpp
│   │   └── Note.h/.cpp
│   ├── plugins/
│   │   ├── VST3PluginInstance.h/.cpp
│   │   └── VST3Scanner.h/.cpp
│   ├── views/
│   │   ├── arrangement/
│   │   │   ├── ArrangementView.h/.cpp          # コンストラクタ、プロジェクト管理、イベントフィルタ
│   │   │   ├── ArrangementView_Headers.cpp     # トラック/フォルダヘッダー生成
│   │   │   ├── ArrangementView_DragDrop.cpp    # D&Dリオーダー（長押し→ドラッグ→ドロップ）
│   │   │   ├── ArrangementGridWidget.h/.cpp    # コンストラクタ、セットアップ、ユーティリティ
│   │   │   ├── ArrangementGridWidget_Painting.cpp  # paintEvent、クリップ/波形描画
│   │   │   ├── ArrangementGridWidget_Input.cpp     # マウス/キーイベント
│   │   │   ├── ArrangementGridWidget_DragDrop.cpp  # MIDI/オーディオファイルD&D
│   │   │   ├── ArrangementGridWidget_Animation.cpp # アニメーション、長押し分割
│   │   │   └── TimelineWidget.h/.cpp
│   │   ├── compose/
│   │   │   └── ComposeView.h/.cpp
│   │   ├── mix/
│   │   │   └── MixView.h/.cpp
│   │   ├── pianoroll/
│   │   │   ├── PianoRollView.h/.cpp
│   │   │   ├── PianoRollGridWidget.h/.cpp          # コンストラクタ、セットアップ、アニメーション
│   │   │   ├── PianoRollGridWidget_Painting.cpp    # paintEvent（グリッド、ノート描画）
│   │   │   ├── PianoRollGridWidget_Input.cpp       # マウス/キー/ホイールイベント
│   │   │   └── VelocityLaneWidget.h/.cpp
│   │   ├── plugineditor/
│   │   │   └── PluginEditorWidget.h/.cpp
│   │   └── source/
│   │       ├── SourceView.h/.cpp              # SourceViewクラス本体
│   │       ├── ScanSpinnerWidget.cpp          # スキャンスピナーウィジェット
│   │       └── PluginLoadOverlay.cpp          # プラグインロードオーバーレイ
│   └── widgets/
│       ├── FaderWidget.h/.cpp
│       ├── IconButton.h
│       ├── KnobWidget.h/.cpp
│       ├── LevelMeterWidget.h/.cpp
│       ├── MixerChannelWidget.h/.cpp
│       ├── ProjectLoadDialog.h/.cpp
│       └── SplashWidget.h/.cpp
├── docs/ (各種ドキュメント)
├── icons/ (SVG/PNGアイコン)
├── Projects/ (サンプルプロジェクト)
└── CMakeLists.txt
```

## 7. 外部依存

| ライブラリ | 用途 |
|-----------|------|
| Qt6 (Core, Gui, Widgets, Concurrent) | UI フレームワーク |
| VST3 SDK (v3.7.10) | プラグインホスティング |
| WASAPI (Windows) | リアルタイムオーディオ出力 |
| CoreAudio / AudioUnit (macOS) | リアルタイムオーディオ入出力 |
| CoreMIDI (macOS) | MIDI入力 |
| AVRT (Windows) | MMCSS スレッドスケジューリング |
| Media Foundation (Windows) | MP3/M4Aオーディオファイルのデコード |
| ExtAudioFile (macOS) | MP3/M4Aオーディオファイルのデコード |

## 8. 将来の拡張

- **オーディオ録音**: WindowsはWASAPI入力キャプチャ、macOSはCoreAudio入力キャプチャ
- **ファイル保存**: JSON/XMLベースのプロジェクトファイル
- **FLACサポート**: FLAC/OGGなど追加オーディオ形式の読み込み
- **MIDI入力**: 外部MIDIキーボード対応
- **オーディオクリップ編集**: トリミング・フェード・タイムストレッチ

## 9. デプロイメントとインストール

### 9.1 概要
Darwin DAW は、Windows では **Inno Setup** によるインストーラーを、
macOS では `package_macos.sh` による `.app` / `.dmg` 生成フローを提供します。
Windows は `windeployqt`、macOS は `macdeployqt` を使って
Qt ランタイムを配布物へ収集します。

### 9.2 インストールフロー
1. **バイナリ収集**: `windeployqt` を使用して、Qtの実行時DLLおよびプラグインをビルドディレクトリに収集。
2. **インストーラー定義**: `installer.iss` スクリプトでパッケージに含めるファイルとショートカットを定義。
3. **インストーラー生成**: Inno Setup コンパイラ（`ISCC.exe`）を実行して `.exe` インストーラーを作成。
4. **macOS パッケージ生成**: `package_macos.sh` が `build/Darwin` から `.app` を組み立て、
   `Info.plist`、`.icns`、Qt Framework を同梱し、必要に応じて `codesign` / notarize / `.dmg` 化を行う。

### 9.3 構成要素
- **実行ファイル**: Windowsは`Darwin.exe`、macOSは`Darwin`
- **macOS app bundle**: `Darwin.app/Contents/MacOS/Darwin`、`Info.plist`、`Resources`、Qt Framework / PlugIns
- **依存DLL**: Qt関連DLL、Microsoft VCRuntime等
- **メタデータ**: デスクトップショートカット、スタートメニュー、アンインストーラー
- **ライセンス**: `LICENSE` (インストーラー内で表示)
