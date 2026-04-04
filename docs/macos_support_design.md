# Darwin DAW - macOS対応設計書

## 1. 目的

既存の Darwin DAW は Windows 専用実装を前提としており、以下の機能が macOS で動作しない。

- リアルタイムオーディオ出力
- オーディオ録音
- MIDI 入力
- VST3 スキャン時のメタデータ取得
- VST3 GUI 埋め込み
- MP3 / M4A デコード
- ビルド / 実行スクリプト

本設計では、既存のアプリケーション構造を維持したまま、macOS でも Windows と同等の主要機能を利用可能にすることを目的とする。

## 2. 方針

### 2.1 既存 API は維持する

`PlaybackController` や各 View から見た公開 API はできるだけ変更しない。
OS 差分は各コンポーネント内部に閉じ込め、既存呼び出し側の改修を最小化する。

対象クラス:

- `AudioEngine`
- `AudioInputCapture`
- `MidiInputDevice`
- `VST3Scanner`
- `PluginEditorWidget`
- `AudioFileReader`
- `CMakeLists.txt`

### 2.2 macOS ネイティブ API を採用する

Qt だけでは低レイテンシな音声入出力や MIDI 入力が不足するため、macOS では以下を使用する。

- オーディオ出力: CoreAudio / AudioUnit (`kAudioUnitSubType_DefaultOutput`)
- オーディオ入力: CoreAudio / AudioUnit (`kAudioUnitSubType_HALOutput`)
- MIDI 入力: CoreMIDI
- 圧縮音声デコード: AudioToolbox / ExtAudioFile
- VST3 GUI 埋め込み: VST3 `kPlatformTypeNSView` + Qt のネイティブビュー

### 2.3 Windows 実装は維持する

既存の WASAPI / WinMM / Media Foundation / HWND 実装は残し、`#ifdef Q_OS_WIN` と `#ifdef Q_OS_MAC` で分岐する。
Windows 向けの挙動変更は必要最小限に留める。

## 3. コンポーネント別設計

### 3.1 AudioEngine

- Windows: 現行の WASAPI 実装を継続しつつ、shared mode の `GetMixFormat()` に従って
  float / PCM / `WAVEFORMATEXTENSIBLE` の実データ形式へ変換して書き込む
- macOS: Default Output AudioUnit を利用
- `RenderCallback` のシグネチャは変更しない
- macOS 側では AudioUnit のレンダーコールバック内で一時バッファを使い、既存のインターリーブ float 仕様へ合わせる
- `sampleRate()` と `bufferSize()` は初期化時に既定デバイスから取得する

### 3.2 AudioInputCapture

- Windows: 現行の WASAPI キャプチャを継続しつつ、
  `WAVEFORMATEXTENSIBLE::Samples.wValidBitsPerSample` を考慮して
  24-bit in 32-bit container などの入力を正しく float L/R に正規化する
- macOS: HAL Output AudioUnit を入力専用で利用
- コールバックで取得した入力を内部で float L/R に正規化し、既存 `CaptureCallback` に渡す
- デフォルト入力デバイスを使用する

### 3.3 MidiInputDevice

- Windows: 現行の WinMM 実装を継続
- macOS: CoreMIDI で全入力ソースを購読する
- 既存通り NoteOn / NoteOff のみ処理対象とする
- 将来 CC 対応が必要になった場合でも、現在の構造へ拡張可能な静的コールバック構成にする

### 3.4 VST3Scanner

- Windows: 既存の安全ローダーを継続
- macOS: VST3 Hosting Module を用いてバンドルから Factory / ClassInfo を取得する
- デフォルトスキャンパスは macOS の user / global / network / app-local の
  標準 VST3 配置を優先順付きで利用する
- Windows でも per-user / shared / app-local の標準 VST3 配置を既定 scan path に含める
- `.vst3` バンドル配下の再帰走査は維持しつつ、バンドル内重複検出は引き続き抑制する
- `moduleinfo.json` / `Info.plist` で不足した情報だけ、別プロセスの probe で Hosting Module を使って補完する
- probe helper は JSON 出力後に即終了し、危険なプラグインアンロードでホスト本体が落ちないようにする
- macOS のメタデータ取得は `moduleinfo.json` → `Info.plist` → Hosting Module probe → bundle identifier の順で補完し、`vendor` / `version` の欠落を減らす
- Windows の bundle 解析では `x86_64-win` 固定にせず、`arm64x-win` / `arm64ec-win` /
  `arm64-win` を含む複数候補を順に試す

### 3.5 PluginEditorWidget

- Windows: 現行の HWND 埋め込みを継続
- macOS: `kPlatformTypeNSView` を用いて Qt ネイティブビューへ直接アタッチする
- macOS の NSView 埋め込みでは Cocoa の point 座標系を前提とし、Windows 由来の DPI 補正を持ち込まない
- ホストコンテナへ focus を集め、`IPlugView::onFocus()` を併用して操作入力が通りやすい状態を作る
- スクロール表示時はプラグインホストから親 NSView チェーンへ clip を伝え、sibling ペインへのはみ出しを防ぐ
- viewport / コンテナのリサイズ後は遅延再同期を掛け、ホスト直下の embedded `NSView` frame も host bounds へ揃える
- macOS ではまずダイレクト表示を優先し、Windows 固有のビットマップキャプチャモードは使わない
- リサイズ対応プラグインは `IPlugFrame::resizeView()` を通じて既存フローを再利用する
- 非リサイズ対応プラグインはスクロールエリアにネイティブサイズで表示する

### 3.6 AudioFileReader

- WAV の手動パースは現行のまま維持
- MP3 / M4A:
  - Windows: Media Foundation
  - macOS: ExtAudioFile で float stereo にデコード
- 呼び出し側の API は変更しない

### 3.7 CMake / 実行スクリプト

- macOS では `enable_language(OBJCXX)` を有効化し、VST3 SDK の `module_mac.mm` / `threadchecker_mac.mm` を追加する
- Windows 専用のリンク設定、`windeployqt`、`.rc` 依存は条件分岐する
- `build.sh` / `run.sh` は OS 判定を入れ、macOS でも同一スクリプト名で利用できるようにする
- 配布用には `package_macos.sh` を追加し、`build/Darwin` から `.app` / `.dmg`、
  必要に応じて `codesign` / notarize まで行えるようにする

### 3.8 UI フォント

- 既存 UI では `Segoe UI` / `Segoe UI Light` / `Roboto Mono` が広く使われている
- `FontManager` を共通のフォント定義元とし、Windows / macOS の両方で family と `QFont` の取得元を一元化する
- macOS では Windows 由来のフォント名を OS 標準フォントへエイリアスする
- 既存の直書きフォント指定は段階的に `FontManager` 経由へ置き換える

## 4. 影響範囲

### 4.1 変更対象

- `CMakeLists.txt`
- `README.md`
- `docs/architecture.md`
- `src/audio/AudioEngine.h`
- `src/audio/AudioEngine.cpp`
- `src/audio/AudioEngineBackend.h`
- `src/audio/AudioEngineBackend.cpp`
- `src/audio/AudioInputCapture.h`
- `src/audio/AudioInputCapture.cpp`
- `src/audio/AudioInputCaptureBackend.h`
- `src/audio/AudioInputCaptureBackend.cpp`
- `src/controllers/MidiInputDevice.h`
- `src/controllers/MidiInputDevice.cpp`
- `src/plugins/VST3Scanner.cpp`
- `src/plugins/VST3MetadataProbe.h`
- `src/plugins/VST3MetadataProbe.cpp`
- `src/views/plugineditor/PluginEditorWidget.cpp`
- `src/common/AudioFileReader.h`
- `src/common/AudioFileReader.cpp`
- `src/common/FontManager.h`
- `src/common/FontManager.cpp`
- `src/main.cpp`
- `build.sh`
- `run.sh`
- `package_macos.sh`

### 4.2 非対象

- Linux 対応の本実装
- MIDI CC / Pitch Bend など Note 以外のリアルタイム入力
- AudioUnit / AUv3 プラグインホスト機能

## 5. リスクと対策

### 5.1 VST3 プラグイン差異

- プラグインにより `NSView` 埋め込みやリサイズ挙動が異なる可能性がある
- 対策:
  - まずは direct attach を基本とする
  - 既存の `IPlugFrame` ベースのリサイズ経路を維持する
  - 添付失敗時は明確なログを出す

### 5.2 オーディオコールバックのリアルタイム性

- AudioUnit コールバック内で重い処理や頻繁な再確保を行うとドロップアウトの原因になる
- 対策:
  - 一時バッファは再利用する
  - 公開 API は変えず、内部だけを差し替える

### 5.3 開発環境差異

- 現環境で macOS 実機ビルドが行えない場合、完全な実機検証が不足する
- 対策:
  - 条件分岐を明示的に分ける
  - 可能な範囲で構文・依存関係の静的確認を行う
  - README に未検証項目を残さないよう、検証結果を明記する

---

## 付録A: 配布パッケージ設計

### A.1 目的

macOS では通常ビルドで `build/Darwin` は生成できるが、そのままでは配布向けの `.app` / `.dmg`、Qt ランタイム同梱、`codesign`、`notarize` まで一貫して行えない。既存の開発用ビルドフローを崩さずに、配布用成果物を生成するスクリプトを追加する。

### A.2 方針

- 既存の `build.sh` / `run.sh` は維持し、通常開発では引き続き `build/Darwin` を使う
- `MACOSX_BUNDLE` への全面移行は行わず、配布時だけ `.app` をステージングする
- `build/Darwin` を `Darwin.app/Contents/MacOS/Darwin` へコピーし、`Info.plist` はスクリプトで生成する
- Qt 依存は `macdeployqt` で `.app` へ収集する
- ローカル確認用でも `macdeployqt` 後に ad-hoc 署名を必ず付け直す
- 配布用では `codesign` identity 指定時に Developer ID 署名、`notarytool` の資格情報が渡された場合だけ notarization を行う
- VST3 ホストとして外部プラグインをロードするため、hardened runtime 用 entitlements には `disable-library-validation`、`allow-jit`、`allow-unsigned-executable-memory` を含める
- 出力先は既定で `build/dist-macos` とし、既存の `.gitignore` で無視される範囲に閉じ込める

## 付録B: メニューショートカット設計

### B.1 目的

ハンバーガーメニューは `QToolButton + QMenu` のポップアップ構成であり、macOS でショートカット表記が表示されない場合がある。macOS でもショートカットを表示し、メニューを閉じている状態でもショートカットが利用できるようにする。

### B.2 方針

- アプリ全体で `styleHints()->setShowShortcutsInContextMenus(true)` を有効化する
- 各 `QAction` に対して `setShortcutVisibleInContextMenu(true)` を設定する
- メニュー用 `QAction` を `MainWindow` 配下で生成し、`QMenu` と `MainWindow::addAction()` の両方に登録する
- `QKeySequence::New/Open/Save/Undo/Redo` は Qt の標準変換に委ね、再生/停止の `Space` は既存の `QShortcut` を継続する

## 付録C: プラグインGUI埋め込み詳細設計

### C.1 入力設計

macOS で VST3 プラグイン GUI は表示されるがノブやスライダーをドラッグできない問題への対策:

- `PluginEditorWidget` 内の DPI 換算は macOS だけ 1.0 固定にし、Cocoa の point 座標をそのまま使う
- `QScrollArea` / viewport はフォーカスを持たせず、ネイティブコンテナへ focus を寄せる
- アタッチ直後にホストコンテナへフォーカスを当て、`onFocus(true)` を送る。クローズ時は `onFocus(false)` を送ってから `removed()` する

### C.2 クリッピング設計

macOS で大型 VST3 プラグイン GUI をスクロールすると、ネイティブ `NSView` がペイン外にはみ出す問題への対策:

- プラグインホストコンテナを起点に、親 `NSView` チェーンへ `CALayer.masksToBounds = YES` を伝播し、native child をクリップする
- スクロールバー値変更、リサイズ、アタッチ直後に clip 状態を同期し、`NSView` / `CALayer` の再レイアウトを促す
- エディタを閉じるときは clip 設定を明示的に戻す
- Windows の HWND 埋め込みやビットマップキャプチャ経路には影響を与えない

### C.3 リサイズ同期設計

Qt viewport のサイズ変化と Cocoa hosted NSView frame が非同期で、リサイズ後にプラグイン表示が追従しない問題への対策:

- `PluginEditorWidget` は `QScrollArea`、viewport、コンテナの `Resize` / `Show` 系イベントも監視し、遅延 `updateScaleMode()` を再投入する
- macOS ではホストコンテナ直下の subview に `NSViewWidthSizable | NSViewHeightSizable` を設定し、host bounds へ frame を揃える
- コンテナサイズ変更後は `updateGeometry()` を呼び、Qt と Cocoa のレイアウト確定タイミング差を減らす
- エディタを閉じるときは `removed()` 後にホスト直下の lingering `NSView` を明示的に外す
- `ComposeView` 側では右ペイン用 `QStackedWidget` と各ページの最小幅を 0 に揃え、splitter のリサイズ挙動を安定させる

## 付録D: VST3スキャン安定化詳細設計

### D.1 スキャン安定化方針

macOS の VST3 スキャンでは、`moduleinfo.json` が存在しないプラグインに対して `Module::create()` を呼ぶとCocoa 初期化時クラッシュやアンロード時クラッシュが発生する。

- `moduleinfo.json` を最優先で読み、metadata だけでカテゴリ判定できた場合はバイナリロードしない
- fallback ロードは helper process (`--vst3-metadata-probe`) に分離し、main thread で `Module::create()` を実行する
- helper は JSON 出力後に `std::_Exit(0)` で即終了し、C++ デストラクタやプラグインの deinit を踏まない
- helper のハング時は本体側でタイムアウトを設け、超過時は kill する

### D.2 メタデータ補完方針

VST3 メタデータは以下の優先順で取得し、上位で得られた値は下位で上書きしない:

1. `moduleinfo.json` — `Vendor` / `Version` / `Category` / `SubCategories`
2. `Info.plist` — `CFBundleShortVersionString`（なければ `CFBundleVersion`）
3. VST3 Hosting Module probe（helper process 経由）— `factory.info().vendor()` / `classInfo.vendor()` / `classInfo.version()` / `classInfo.category()`
4. `CFBundleIdentifier` による vendor 補完（最後の手段）
