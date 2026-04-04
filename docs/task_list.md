# Darwin DAW - 機能実装タスク

## Phase 1: 基盤インフラ (Core Infrastructure)
- [x] 設計ドキュメント作成
    - [x] アーキテクチャ設計書 (docs/architecture.md)
    - [x] クラス図 (docs/class_diagram.md)
    - [x] データフロー図 (docs/data_flow.md)
    - [x] コーディング規約 (docs/coding_standards.md)
- [x] データモデル定義
    - [x] Project/Track/Clip/Noteクラス設計
    - [x] シリアライズ/デシリアライズ
- [x] シグナル/スロット接続基盤
    - [x] 各ビュー間のイベント伝播設計

## Phase 2: トランスポート (Transport)
- [x] 再生/停止機能
    - [x] 再生状態の管理
    - [x] 再生ボタンアイコン切替
    - [x] 再生ヘッドアニメーション
- [x] BPM/タイムコード
    - [x] BPM値編集（ダブルクリック）
    - [x] タイムコード表示更新

## Phase 3: Instrument & VST3 Infrastructure [COMPLETE]
- [x] Create Source View structure (List, Detail Card)
- [x] Implement VST3 Scanning (Recursive search, category detection)
- [x] Parallel Scanning & Source View Integration
- [x] Persistent plugin usage tracking & sorting
- [x] Scan Settings UI (Paths management)

## Phase 4: Compose View - Arrangement [COMPLETE]
- [x] トラック管理
    - [x] トラック追加機能 (SourceView連携)
    - [x] トラック削除機能
    - [x] 表示/非表示切替（目アイコン機能）
- [x] クリップ操作
    - [x] クリップデータモデル
    - [x] クリップ描画
    - [x] クリップ選択
    - [x] クリップ移動（ドラッグ）
    - [x] クリップリサイズ
    - [x] クリップ作成（ダブルクリック）
- [x] スクロール同期
    - [x] Arrangement/PianoRoll水平スクロール同期

## Phase 5: Compose View - Piano Roll [COMPLETE]
- [x] ノート編集
    - [x] ノートデータモデル
    - [x] ノート描画（カスタムペイント）
    - [x] ノート選択
    - [x] ノート作成（ダブルクリック）
    - [x] ノート移動（ドラッグ）
    - [x] ノートリサイズ
    - [x] ノート削除（ダブルクリック/Delete）
- [x] ゴーストノート
    - [x] 他トラックノート半透明表示
    - [x] 非表示トラックのゴーストノート非表示
- [x] ベロシティ
    - [x] ベロシティレーン表示
    - [x] ベロシティ編集
- [x] トラックカラー
    - [x] カラー選択UI
    - [x] トラック色でノート/クリップ表示
- [x] タイムラインズーム
    - [x] Ctrl+スクロール/長押しドラッグによるズーム
    - [x] ズームに応じたグリッド線（32分音符まで）
    - [x] クリップ範囲表示・制限

## Phase 6: VST3 ホスティング [COMPLETE]
- [x] CMake/ビルド設定
    - [x] VST3 SDK ヘッダー・ライブラリのリンク設定
- [x] プラグインロード基盤
    - [x] VST3PluginHost（DLLロード、ファクトリ取得）
    - [x] VST3PluginInstance（コンポーネント初期化、ライフサイクル管理）
- [x] プラグインGUI表示
    - [x] PluginEditorWidget（IPlugView → Qtネイティブビュー埋め込み）
    - [x] IPlugFrame 実装（リサイズ対応）
- [x] トラック統合
    - [x] Track にプラグインインスタンス紐づけ
    - [x] loadPlugin / unloadPlugin メソッド
- [x] SourceView 統合
    - [x] 左パネルにトラック一覧表示
    - [x] トラック選択でプラグインGUI表示
    - [x] LOAD INSTRUMENT でトラックにプラグイン割り当て

## Phase 7: Mix View - ミキサー
- [/] 実際のトラックとマスターを表示する
    - [x] Project/Trackモデルと連携してMixerViewを構築
    - [x] `MixerChannelWidget`の動的生成
    - [x] マスターチャンネルの追加表示
- [x] フェーダー操作
    - [x] フェーダードラッグ操作
    - [x] ボリューム値表示
- [x] ノブ操作
    - [x] Panノブドラッグ
    - [x] Timingノブドラッグ
    - [x] ノブ値リアルタイム表示
- [x] FXスロット
    - [x] FX追加ダイアログ
    - [x] FX有効/無効（バイパス）
- [x] マスターおよび各トラック
    - [x] L/Rレベルメーター描画 (インディビジュアル＆マスター)
    - [x] メーターアニメーション

## Phase 8: Mix View - インスペクター
- [x] トラック情報表示
    - [x] 選択トラック名/音源表示

## Phase 9: オーディオエンジン [COMPLETE]
- [x] 基本オーディオ出力
    - [x] OSネイティブ音声APIベースのエンジン (AudioEngine)
    - [x] Windows: MMCSSレンダリングスレッド / macOS: AudioUnitコールバック
    - [x] コールバック方式でのバッファ充填
- [x] MIDIシーケンサー
    - [x] PlaybackControllerからクリップ/ノートのMIDIイベント収集
    - [x] NoteOn/NoteOff管理（ActiveNoteトラッキング）
    - [x] VST3PluginInstance::processAudio() によるMIDI→オーディオ変換
- [x] オーディオミキシング
    - [x] トラックごとのボリューム・パン適用
    - [x] 全トラックのミックスダウン
    - [x] インターリーブ出力（OSネイティブ出力対応）

## Phase 11: macOS対応 [COMPLETE]
- [x] CMakeでApple向けVST3 Hostingソースとフレームワークを条件付きリンク
- [x] AudioEngine / AudioInputCapture を macOS の CoreAudio / AudioUnit に対応
- [x] MidiInputDevice を CoreMIDI に対応
- [x] VST3Scanner と PluginEditorWidget を macOS のバンドル / NSView に対応
- [x] AudioFileReader の MP3 / M4A 読み込みを ExtAudioFile に対応
- [x] README / 設計書 / 実行スクリプトを macOS 向けに更新
- [x] UIフォント管理を FontManager に集約し、Windows / macOS の両方で一元制御できるようにした
- [x] macOS のハンバーガーメニューでもショートカット表示と発火が安定するよう改善
- [x] macOS の VST3 スキャンで、fallback ロードを main thread に寄せてクラッシュしにくくした
- [x] macOS の VST3 スキャンで、`Info.plist` と Hosting Module を使って vendor / version を補完できるようにした
- [x] macOS の VST3 スキャンで、危険な Hosting Module fallback を helper process に隔離した
- [x] macOS の PluginEditorWidget で、NSView の座標系と focus を見直して GUI を操作しやすくした
- [x] macOS の PluginEditorWidget で、スクロール時に NSView がペイン外へはみ出さないよう native viewport clip を追加した
- [x] PluginEditorWidget で、Windows / macOS ともに親UIとの入力競合時はプラグインビューを優先するよう調整した
- [x] macOS の PluginEditorWidget で、viewport リサイズと embedded NSView の frame 同期を追加し、ペイン拡大縮小への追従を安定化した
- [x] macOS の配布向けに `.app` / `.dmg` 生成、任意の `codesign` / notarize を行う `package_macos.sh` を追加した

## Phase 10: コード整備 [COMPLETE]
- [x] フォルダリオーガナイズ
    - [x] views/ をドメイン別サブフォルダに分割
        - [x] arrangement/, pianoroll/, compose/, mix/, source/, plugineditor/
    - [x] CMakeLists.txt の include_directories 更新
- [x] ドキュメント更新
    - [x] architecture.md 更新
    - [x] class_diagram.md 更新
    - [x] data_flow.md 更新
    - [x] coding_standards.md 更新
    - [x] task_list.md 更新

## Future: 追加機能 [COMPLETE]
- [x] プロジェクト保存/読み込み（シリアライズ）
    - [x] Note/Clip/Track/Project JSON シリアライズ/デシリアライズ
    - [x] ファイルメニュー（新規/開く/保存/名前を付けて保存）
- [x] オーディオファイルエクスポート
    - [x] AudioExporter クラス（WAVオフラインレンダリング）
    - [x] 24bit/16bit PCM WAV出力
    - [x] 進捗ダイアログ表示
- [x] Undo/Redo システム
    - [x] QUndoStack/QUndoCommand 基盤
    - [x] ノート操作コマンド（追加/削除/移動/リサイズ/ベロシティ変更）
    - [x] クリップ操作コマンド（追加/削除/移動/リサイズ）
    - [x] トラック操作コマンド（追加/削除）
    - [x] Edit メニュー（Undo: Ctrl+Z / Redo: Ctrl+Y）
- [x] クリップ、選択ノートのコピペ
    - [x] ピアノロール: ノートのコピー/カット/ペースト（Ctrl+C/X/V）
    - [x] アレンジメント: クリップのコピー/カット/ペースト（Ctrl+C/X/V）
    - [x] システムクリップボード経由JSON形式

## Future: 追加機能2
- [x] 操作時の気持のいいアニメーション
- [x] 大規模ファイルの分割リファクタリング
    - [x] ArrangementGridWidget.cpp → 4分割ファイル (_Painting, _Input, _DragDrop, _Animation)
    - [x] PianoRollGridWidget.cpp → 2分割ファイル (_Painting, _Input)
    - [x] SourceView.cpp → 2分割ファイル (ScanSpinnerWidget.cpp, PluginLoadOverlay.cpp)
    - [x] ArrangementView.cpp → 2分割ファイル (_Headers, _DragDrop)
    - [x] MixView リファクタリング（定数抽出、ヘルパーメソッド化、シグナル切断バグ修正）
- [ ] 小節表示部分にエクスポート範囲を指定できるUIを追加
- [ ] ピアノロールのコード表示
- [x] トラックの順番を入れ替えられる機能追加してください。
トラックを長押しするとつかみあがったかのようなアニメーション演出が再生され表示になり、自由に順番を入れ替えられます。
上とは関係ないですがクリップのトラック間移動も実装してください。
アイコンがかっこよくアニメーションで出てくる起動時の演出も追加
- [x] トラックフォルダ機能
    - [x] Track モデルに isFolder / parentFolderId / folderExpanded 追加
    - [x] Project にフォルダ管理メソッド追加 (addFolderTrack, addTrackToFolder, removeTrackFromFolder, folderChildren, isTrackVisibleInHierarchy, moveFolderBlock)
    - [x] ArrangementView にフォルダUI（展開/折りたたみ、D&Dでフォルダに格納/並び替え）
    - [x] MixView にフォルダチャンネル表示（カラーセパレーター + フェーダー/パン/FX付き）
    - [x] ArrangementGridWidget の可視トラック制御（折りたたみ時の行非表示）
    - [x] シリアライズ/デシリアライズ対応
    - [x] コンポーズ↔ミックス間のフォルダ状態連動
    - [x] D&Dでトラックをフォルダにドロップして格納
    - [x] フォルダのD&D並び替え（子トラックをブロック移動）
    - [x] 右クリックコンテキストメニュー削除（D&D操作に統一）

- [ ] 再生ヘッドのアニメーション
    - [ ] 再生状態の判定ロジック追加
    - [ ] TimelineWidget、ArrangementGridWidget、PianoRollGridWidgetにおけるグラデーション軌跡描画

---
**凡例**: [ ] 未着手 / [/] 進行中 / [x] 完了
