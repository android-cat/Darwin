# Darwin DAW - 補足設計書

## 1. オーディオバックエンド抽象化

### 1.1 目的

Windows の WASAPI と macOS の AudioUnit / CoreAudio 実装を `AudioEngine` / `AudioInputCapture` の内部バックエンドとして分離し、将来的に ASIO など別ドライバーを追加できる土台を整える。呼び出し側 API は維持しつつ、プラットフォーム依存コードを公開クラスの外へ退避する。

### 1.2 方針

- `AudioEngine` は `AudioEngineBackend` を 1 つ保持し、`AudioInputCapture` は `AudioInputCaptureBackend` を 1 つ保持する
- 実体生成はファクトリ関数で行い、ビルド対象 OS に応じて適切な実装を返す
- 公開 API（`initialize()` / `start()` / `stop()` / `isRunning()` / `sampleRate()` / `bufferSize()` / `setRenderCallback()` 等）は変更しない
- コールバックは公開クラス側が保持し、バックエンドは Context 経由で「音声データを上位へ渡す関数」を受け取る

### 1.3 クラス構成

- `AudioEngine` → `AudioEngineBackend` → `WasapiAudioEngineBackend` / `MacAudioEngineBackend`
- `AudioInputCapture` → `AudioInputCaptureBackend` → `WasapiAudioInputCaptureBackend` / `MacAudioInputCaptureBackend`

## 2. FontManager

### 2.1 目的

UI に `Segoe UI` / `Roboto Mono` などのフォント名が複数箇所に直書きされている状態を解消し、`FontManager` をアプリ全体のフォント定義元として一元管理する。

### 2.2 方針

- `main.cpp` の起動処理から `configureApplicationFonts()` を 1 回だけ呼ぶ
- 各 View / Widget はフォント名を直書きせず、`FontManager` の family / `QFont` / CSS ヘルパーを使う
- Windows は `Segoe UI` / `Roboto Mono` 優先、macOS は `QFontDatabase::systemFont()` を使う
- 旧来フォント名のエイリアス登録で移行途中の見た目崩れを防ぐ

### 2.3 公開 API

- `configureApplicationFonts(QApplication&)` / `uiFontFamily()` / `monoFontFamily()` / `uiFontCss()` / `monoFontCss()` / `uiFont(int, int)` / `monoFont(int, int)`

## 3. ピアノロール貼り付け選択

### 3.1 目的

Piano Roll 上で複数ノートを `Ctrl+V` で貼り付けた直後、貼り付け対象のノート群を選択状態に保ち、ユーザーが貼り付け直後にそのままドラッグしてまとめて移動できるようにする。

### 3.2 方針

- 貼り付け処理中に生成した `Note*` を順番に収集する
- 貼り付け成功後は収集結果を `m_selectedNotes` に反映し、`m_selectedNote` には最後に貼り付けたノートを設定する
- 貼り付け成功ノートが 0 件の場合は既存の選択状態を変更しない

## 4. プラグインエディタ入力優先

### 4.1 目的

`PluginEditorWidget` 内でプラグイン GUI を操作しているとき、親の `QScrollArea`、`QSplitter`、残留ウィジェットなどとマウス / ホイール / ジェスチャ入力が競合する問題を、プラグインビュー優先で解決する。

### 4.2 方針

- コンテナ / キャプチャ表示に `WA_NoMousePropagation` を設定する
- viewport に event filter を入れ、マウス / ホイール / ネイティブジェスチャ / タブレット系イベントをプラグイン実表示領域に入っている場合だけ打ち止めにする
- スケール表示ラベルは `WA_TransparentForMouseEvents` にして常にプラグイン本体より下位の扱いにする
- エディタを閉じるときは優先設定を解除し、関連ウィジェットを先に `hide()` してから破棄する
- Windows のビットマップキャプチャ経路でも `ScaledPluginDisplay` が明示的に `accept()` して親イベントと競合しないようにする

## 5. テスト実装

### 5.1 目的

モデル層・Undo/Redo・共通ユーティリティの回帰検知を自動化する。Qt Test フレームワークで `DarwinTests` 実行ファイルを追加し、`ctest` から一括実行できる基盤を整える。

### 5.2 対象

| レイヤー | 対象 |
|----------|------|
| Model | `Note`, `Clip`, `Track`, `Project` |
| Command | `UndoCommands` |
| Common | `WavWriter`, `AudioFileReader`, `ThemeManager`, `FontManager`, `ModelAccessLock` |

### 5.3 テスト方針

- Qt 標準の `QtTest` を採用し、`QApplication` ベースで起動する
- 一時ディレクトリを用いて WAV / project JSON の実ファイル round-trip を検証する
- 値の clamp・所有権移動・JSON シリアライズ・Undo/Redo/merge・WAV 書き出しと再読込・テーマ/フォント API の整合性をカバーする
- `BUILD_TESTING` 有効時のみ `Qt6::Test` と `DarwinTests` を追加する

## 6. Windows VST3 スキャン信頼性改善

### 6.1 目的

Windows 環境で `.vst3` が存在しても Darwin のスキャン結果に表示されないケースを減らす。

### 6.2 方針

- instrument 一覧は `isInstrument` を満たすことを基準にする（`Fx|Instrument` の dual category も含む）
- factory の class info は `Audio Module Class` のみを分類対象にし、非オーディオ class による `isEffect` の誤付与を避ける
- scan path に `.vst3` そのものが直接指定された場合もそのまま解析する
- 既定 scan path に `%LOCALAPPDATA%/Programs/Common/VST3`（per-user）、`%COMMONPROGRAMFILES%/VST3`（shared）、`$APPFOLDER/VST3`（同梱）を追加する
- Windows bundle は `arm64x-win` / `arm64ec-win` / `arm64-win` / `x86_64-win` / `x86-win` を優先順付きで探索し、既知候補で見つからない場合は `Contents/*-win/*.vst3` を動的列挙する
