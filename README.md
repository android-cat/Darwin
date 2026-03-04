# Darwin DAW

Qt6/C++ で構築されたオープンソースのデジタルオーディオワークステーション（DAW）です。  
モダンでミニマルなUIデザインと、直感的な操作性を重視しています。

## 主な機能

- **アレンジメントビュー** — トラックの管理・クリップの配置・フォルダトラックのネスト
- **ピアノロール** — ノートの編集・ベロシティレーン・クォンタイズ対応
- **ミキサービュー** — フォルダトラック対応のミキシングコンソール・フェーダー・ノブ
- **VST3 プラグインホスティング** — VST3 インストゥルメント/エフェクトのスキャン・ロード・GUI 表示
- **リアルタイムオーディオ出力** — WASAPI 共有モード経由
- **オーディオファイル対応** — WAV / MP3 / M4A のインポート・波形表示・再生
- **MIDI ファイルインポート**
- **アンドゥ/リドゥ** — コマンドパターンによる完全な操作履歴管理
- **テーマ管理** — カスタムダークテーマ

## 動作環境

| 項目 | 要件 |
|------|------|
| OS | Windows 10/11 (64-bit) |
| Qt | 6.10.2 (Widgets, Concurrent, Svg) |
| CMake | 3.16 以上 |
| コンパイラ | MSVC 2019 以上（C++17） |
| VST3 SDK | ビルド時に自動ダウンロード |

> **注意**: 現バージョンは Windows 専用です（WASAPI・Win32 VST3 モジュールを使用）。

## ビルド手順

### 前提条件

- [Qt 6](https://www.qt.io/download-open-source) をインストールし、`Qt6_DIR` または `PATH` に設定
- [CMake 3.16+](https://cmake.org/download/) をインストール
- Visual Studio 2019 以上（C++ デスクトップ開発ワークロードが必要）

### ビルド

```bash
# リポジトリのクローン
git clone https://github.com/android-cat/Darwin.git
cd Darwin

# CMake 設定（VST3 SDK は自動でダウンロードされます）
cmake -B build -DCMAKE_BUILD_TYPE=Release

# ビルド
cmake --build build --config Release
```

ビルド完了後、`build/Release/Darwin.exe` が生成されます。  
`windeployqt` により Qt の DLL が自動でコピーされます。

## プロジェクト構成

```
Darwin/
├── src/
│   ├── main.cpp
│   ├── MainWindow.cpp/.h
│   ├── audio/          # AudioEngine (WASAPI), AudioExporter
│   ├── commands/       # UndoCommands (Command パターン)
│   ├── common/         # 定数, テーマ, MIDI/オーディオユーティリティ
│   ├── controllers/    # PlaybackController
│   ├── models/         # Project / Track / Clip / Note
│   ├── plugins/        # VST3Scanner, VST3PluginInstance
│   ├── views/          # Arrangement, PianoRoll, Mix, Compose, Source
│   └── widgets/        # 汎用 UI コンポーネント
├── docs/               # アーキテクチャ設計書
├── icons/              # アプリケーションアイコン
├── CMakeLists.txt
└── resources.qrc
```

## ドキュメント

- [アーキテクチャ設計書](docs/architecture.md)
- [クラス図](docs/class_diagram.md)
- [データフロー](docs/data_flow.md)

## ライセンス

このプロジェクトは [GNU General Public License v3.0](LICENSE) のもとで公開されています。  
VST3 SDK (Steinberg Media Technologies) は GPL v3 ライセンスのもとで使用しています。

## 謝辞

- [Steinberg VST3 SDK](https://github.com/steinbergmedia/vst3sdk) — GPL v3
- [Qt Framework](https://www.qt.io/) — LGPL v3
