# Darwin DAW

Qt6/C++ で構築されたオープンソースのデジタルオーディオワークステーション（DAW）です。  
モダンでミニマルなUIデザインと、直感的な操作性を重視しています。

## 主な機能

- **アレンジメントビュー** — トラックの管理・クリップの配置・フォルダトラックのネスト・クリップ分割・ドラッグ&ドロップ並べ替え
- **ピアノロール** — ノートの編集・ベロシティレーン・クォンタイズ対応
- **エクスプレッションレーン** — MIDI CC (CC1–127)・ピッチベンド・チャンネルアフタータッチのブレークポイント編集
- **ミキサービュー** — フォルダトラック対応の階層型ミキシングコンソール・フェーダー・ノブ・ミュート/ソロ/パン
- **VST3 プラグインホスティング** — VST3 インストゥルメント/エフェクトのスキャン・ロード・GUI 表示（プラグインエディタビュー）
- **リアルタイムオーディオ出力** — Windows は WASAPI、macOS は CoreAudio / AudioUnit 経由
- **オーディオファイル対応** — WAV / MP3 / M4A のインポート・波形表示・再生
- **録音入力** — Windows は WASAPI キャプチャ、macOS は CoreAudio 入力
- **MIDI 入力** — Windows は WinMM、macOS は CoreMIDI
- **MIDI ファイルインポート**
- **マーカー/フラグ** — タイムライン上のマーカー配置・ナビゲーション
- **エクスポート範囲選択** — タイムライン上で任意の範囲を指定してオーディオエクスポート
- **コード検出** — ノートからリアルタイムにコード名を検出（メジャー/マイナー/ディミニッシュ/オーギュメント/sus/7th 等）
- **アンドゥ/リドゥ** — コマンドパターンによる完全な操作履歴管理
- **テーマ管理** — カスタムダークテーマ・統一フォント管理

## 動作環境

| 項目 | 要件 |
|------|------|
| OS | Windows 10/11 (64-bit), macOS 13 以降 |
| Qt | 6.10.2 (Widgets, Concurrent, Svg) |
| CMake | 3.16 以上 |
| コンパイラ | MSVC 2019 以上 / Apple Clang（C++17） |
| VST3 SDK | ビルド時に自動ダウンロード |

> **注意**: 現在の正式サポートOSは Windows / macOS です。Linux 向けのランタイム実装は含まれていません。

## ビルド手順

### 前提条件

- [Qt 6](https://www.qt.io/download-open-source) をインストールし、`Qt6_DIR` または `PATH` に設定
- [CMake 3.16+](https://cmake.org/download/) をインストール
- Windows: Visual Studio 2019 以上（C++ デスクトップ開発ワークロードが必要）
- macOS: Xcode Command Line Tools をインストール（`build.sh` は `Ninja` が無ければ `Unix Makefiles` を使用）

### ビルド

```bash
# リポジトリのクローン
git clone https://github.com/android-cat/Darwin.git
cd Darwin

# CMake 設定（VST3 SDK は自動でダウンロードされます）
# Qt の場所が自動検出されない場合は CMAKE_PREFIX_PATH を追加してください
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# ビルド
cmake --build build --config Release
```

ビルド完了後の実行ファイル:

- Windows: `build/Release/Darwin.exe`
- macOS: `build/Darwin`

Windows では `windeployqt` により Qt の DLL が自動でコピーされます。

## macOS 配布パッケージ

macOS では通常ビルドとは別に、配布用 `.app` / `.dmg` を
`package_macos.sh` で生成できます。

### ローカル確認用の ad-hoc 署名で `.app` / `.dmg` を作る

```bash
./package_macos.sh
```

既定の出力先:

- `.app`: `build/dist-macos/Darwin.app`
- `.dmg`: `build/dist-macos/Darwin-<version>-macOS.dmg`

### Developer ID 署名付きで作る

```bash
./package_macos.sh \
  --codesign-identity "Developer ID Application: Example Corp (TEAMID)"
```

### notarization まで行う

`notarytool` の keychain profile を使う場合:

```bash
./package_macos.sh \
  --codesign-identity "Developer ID Application: Example Corp (TEAMID)" \
  --notary-profile darwin-notary
```

直接資格情報を渡す場合:

```bash
./package_macos.sh \
  --codesign-identity "Developer ID Application: Example Corp (TEAMID)" \
  --apple-id "name@example.com" \
  --apple-password "app-specific-password" \
  --team-id "TEAMID"
```

### 補足

- `package_macos.sh` は `build/Darwin` を元に `.app` を段階生成し、
  `macdeployqt` で Qt ランタイムを同梱します
- `--codesign-identity` を省略した場合でも、`macdeployqt` 後に
  ローカル起動用の ad-hoc 署名を自動で付け直します
- VST3 ホスト向けに `packaging/macos/Darwin.entitlements` を使用します
- 既存ビルドを使いたい場合は `--skip-build` を指定できます
- `.dmg` が不要な場合は `--no-dmg` を使ってください
- `macdeployqt` を自動検出できない場合は `DARWIN_MACDEPLOYQT` か
  `--macdeployqt` で明示できます
- Gatekeeper 配布や notarization には、引き続き Developer ID 署名が必要です

## プロジェクト構成

```
Darwin/
├── src/
│   ├── main.cpp
│   ├── MainWindow.cpp/.h
│   ├── audio/          # AudioEngine, AudioEngineBackend, AudioInputCapture, AudioExporter
│   ├── commands/       # UndoCommands (Command パターン)
│   ├── common/         # 定数, テーマ, ChordDetector, FontManager, MIDI/オーディオユーティリティ
│   ├── controllers/    # PlaybackController, MidiInputDevice
│   ├── models/         # Project / Track / Clip / Note / CCEvent
│   ├── plugins/        # VST3Scanner, VST3PluginInstance, VST3MetadataProbe
│   ├── views/          # Arrangement, PianoRoll, Mix, Compose, Source, PluginEditor
│   └── widgets/        # Fader, Knob, LevelMeter, MixerChannel 等の汎用 UI コンポーネント
├── docs/               # アーキテクチャ・コーディング規約等の設計書
├── icons/              # アプリケーションアイコン
├── packaging/          # macOS 配布用リソース (entitlements 等)
├── CMakeLists.txt
└── resources.qrc
```

## ドキュメント

- [アーキテクチャ設計書](docs/architecture.md)
- [macOS対応設計書](docs/macos_support_design.md)
- [クラス図](docs/class_diagram.md)
- [データフロー](docs/data_flow.md)
- [コーディング規約](docs/coding_standards.md)
- [補足設計書](docs/supplementary_designs.md)
- [タスクリスト](docs/task_list.md)

## ライセンス

このプロジェクトは [GNU General Public License v3.0](LICENSE) のもとで公開されています。  
VST3 SDK (Steinberg Media Technologies) は GPL v3 ライセンスのもとで使用しています。

## 謝辞

- [Steinberg VST3 SDK](https://github.com/steinbergmedia/vst3sdk) — GPL v3
- [Qt Framework](https://www.qt.io/) — LGPL v3
