# Darwin DAW - データフロー図

## 1. 再生時のデータフロー（オーディオエンジン統合）

```mermaid
sequenceDiagram
    participant User
    participant MainWindow
    participant PlaybackController
    participant AudioEngine
    participant VST3PluginInstance
    participant Project
    participant ArrangementGridWidget
    participant PianoRollGridWidget

    User->>MainWindow: 再生ボタンクリック
    MainWindow->>PlaybackController: togglePlayPause()
    PlaybackController->>PlaybackController: 全トラックのprepareAudio()
    PlaybackController->>AudioEngine: start()
    AudioEngine->>AudioEngine: オーディオストリーム開始
    AudioEngine->>AudioEngine: プラットフォーム別コールバック起動

    loop オーディオコールバック駆動（~10ms前後）
        AudioEngine->>PlaybackController: audioRenderCallback(buffer, frames)
        PlaybackController->>PlaybackController: クリップ種別を判定
        alt オーディオクリップ
            PlaybackController->>PlaybackController: PCMデータをtick位置から読み出し（線形補間）
        end
        alt MIDIクリップ + プラグインあり
            PlaybackController->>PlaybackController: MIDIイベントを収集（クリップ/ノートから）
            PlaybackController->>VST3PluginInstance: processAudio(outL, outR, frames, midiEvents)
            VST3PluginInstance-->>PlaybackController: オーディオデータ
        end
        PlaybackController->>PlaybackController: トラックをミックス（vol/pan適用）
        PlaybackController-->>AudioEngine: バッファ充填完了
        AudioEngine->>AudioEngine: OSネイティブ出力へ反映
    end

    loop 16ms毎（UI更新タイマー）
        PlaybackController->>Project: setPlayheadPosition(pos)
        PlaybackController-->>MainWindow: positionChanged(pos)
        MainWindow->>MainWindow: updateTimecode()
        PlaybackController-->>ArrangementGridWidget: positionChanged(pos)
        PlaybackController-->>PianoRollGridWidget: positionChanged(pos)
        ArrangementGridWidget->>ArrangementGridWidget: update()
        PianoRollGridWidget->>PianoRollGridWidget: update()
    end

    User->>MainWindow: 停止ボタンクリック
    MainWindow->>PlaybackController: togglePlayPause()
    PlaybackController->>AudioEngine: stop()
```

## 2. プラグインロード・GUI表示のデータフロー

```mermaid
sequenceDiagram
    participant User
    participant SourceView
    participant MainWindow
    participant Project
    participant Track
    participant VST3PluginInstance
    participant PluginEditorWidget

    User->>SourceView: LOAD INSTRUMENTクリック
    SourceView-->>MainWindow: loadInstrumentRequested(name, path)
    MainWindow->>Project: addTrack(name)
    Project-->>MainWindow: trackAdded(Track*)
    MainWindow->>Track: loadPlugin(path)
    Track->>VST3PluginInstance: new + load(path)
    VST3PluginInstance->>VST3PluginInstance: モジュールロード
    VST3PluginInstance->>VST3PluginInstance: コンポーネント初期化
    VST3PluginInstance->>VST3PluginInstance: オーディオプロセッサセットアップ
    VST3PluginInstance-->>Track: ロード成功

    User->>SourceView: トラック選択
    SourceView->>PluginEditorWidget: openEditor(instance)
    PluginEditorWidget->>VST3PluginInstance: createView()
    VST3PluginInstance-->>PluginEditorWidget: IPlugView*
    PluginEditorWidget->>PluginEditorWidget: ネイティブビュー作成・アタッチ
    PluginEditorWidget->>PluginEditorWidget: スケールモード判定
```

## 3. ノート作成のデータフロー

```mermaid
sequenceDiagram
    participant User
    participant PianoRollGridWidget
    participant Clip
    participant Note

    User->>PianoRollGridWidget: ダブルクリック
    PianoRollGridWidget->>PianoRollGridWidget: 描画位置からpitch, start計算
    PianoRollGridWidget->>Clip: addNote(pitch, start, duration, velocity)
    Clip->>Note: new Note(...)
    Clip-->>PianoRollGridWidget: noteAdded(note)
    PianoRollGridWidget->>PianoRollGridWidget: update()
```

## 4. クリップ操作のデータフロー

```mermaid
sequenceDiagram
    participant User
    participant ArrangementGridWidget
    participant Clip
    participant PianoRollGridWidget

    User->>ArrangementGridWidget: ダブルクリック（空白部分）
    ArrangementGridWidget->>ArrangementGridWidget: トラック/位置を計算
    ArrangementGridWidget->>ArrangementGridWidget: 新規Clip作成

    User->>ArrangementGridWidget: クリップをクリック
    ArrangementGridWidget-->>PianoRollGridWidget: clipSelected(clip)
    PianoRollGridWidget->>PianoRollGridWidget: setActiveClip(clip)

    User->>ArrangementGridWidget: クリップをドラッグ
    ArrangementGridWidget->>Clip: setStartTick(newPos)
    Clip-->>ArrangementGridWidget: changed()
    ArrangementGridWidget->>ArrangementGridWidget: update()
```

## 5. オーディオレンダリングの詳細フロー

```mermaid
flowchart TD
    A[オーディオコールバック発火] --> B[AudioEngine: 利用可能フレーム数取得]
    B --> C[PlaybackController: audioRenderCallback]
    C --> D[再生位置範囲を計算]
    D --> E{各トラックをループ}
    E --> F[アクティブノートのNoteOff収集]
    F --> G[クリップからNoteOnイベント収集]
    G --> H[MIDIイベントをソート]
    H --> I[VST3PluginInstance::processAudio]
    I --> J[トラック出力をミックス<br>ボリューム・パン適用]
    J --> E
    E --> K[インターリーブして出力バッファに書込]
    K --> L[再生位置を更新<br>m_playPositionTicks]
```

## 6. ビュー間の同期

```mermaid
flowchart LR
    subgraph ComposeView
        AV[ArrangementView]
        PV[PianoRollView]
        PE[PluginEditorWidget]
    end

    subgraph Models
        Project
        Track
        Clip
    end

    subgraph Controllers
        PC[PlaybackController]
    end

    subgraph Audio
        AE[AudioEngine]
        VP[VST3PluginInstance]
    end

    PC -->|positionChanged| AV
    PC -->|positionChanged| PV
    PC -->|audioRenderCallback| VP
    AE -->|renderCallback| PC

    AV -->|clipSelected| PV
    PV -->|noteEdited| Clip
    Clip -->|changed| PV
```

## 7. シグナル接続一覧

| 発行元 | シグナル | 受信先 | スロット |
|--------|----------|--------|----------|
| `PlaybackController` | `playStateChanged(bool)` | `MainWindow` | `onPlayStateChanged()` |
| `PlaybackController` | `positionChanged(qint64)` | `MainWindow` | `onPlayheadPositionChanged()` |
| `PlaybackController` | `positionChanged(qint64)` | `ArrangementGridWidget` | `setPlayheadPosition()` |
| `PlaybackController` | `positionChanged(qint64)` | `PianoRollGridWidget` | `setPlayheadPosition()` |
| `ArrangementGridWidget` | `clipSelected(Clip*)` | `PianoRollGridWidget` | `setActiveClip()` |
| `SourceView` | `loadInstrumentRequested(QString, QString)` | `MainWindow` | (lambda) |
| `Project` | `trackAdded(Track*)` | 複数 | `update()` |
| `Track` | `propertyChanged()` | `ArrangementGridWidget` | `update()` |
| `Clip` | `noteAdded(Note*)` | `PianoRollGridWidget` | `update()` |
| `Clip` | `noteRemoved(Note*)` | `PianoRollGridWidget` | `update()` |
| `QDoubleSpinBox` | `valueChanged(double)` | `MainWindow` | `onBpmChanged()` |
| `Project` | `flagsChanged()` | `TimelineWidget` | `update()` |
| `Project` | `flagsChanged()` | `TimelineWidget` | `update()` |
| `MainWindow (skipPrevBtn)` | `clicked()` | `PlaybackController` | `seekTo(prevFlag)` |
| `MainWindow (skipNextBtn)` | `clicked()` | `PlaybackController` | `seekTo(nextFlag)` |
| `ThemeManager` | `themeChanged()` | 複数 | 各UIのスタイル更新・再描画 |

## 8. Undo/Redo のデータフロー

```mermaid
sequenceDiagram
    participant User
    participant View (PianoRoll等)
    participant QUndoStack
    participant UndoCommand
    participant Model (Clip/Track等)

    User->>View: 編集操作（ドラッグ、追加等）
    View->>UndoCommand: コマンド生成 (new MoveNoteCommand等)
    View->>QUndoStack: push(command)
    QUndoStack->>UndoCommand: redo()
    UndoCommand->>Model: データを変更
    Model-->>View: changed() / noteAdded() 等の通知
    View->>View: update() (再描画)

    User->>View: Ctrl+Z (Undo)
    View->>QUndoStack: undo()
    QUndoStack->>UndoCommand: undo()
    UndoCommand->>Model: データを元に戻す
    Model-->>View: 通知
```

## 9. オーディオエクスポートのデータフロー

```mermaid
sequenceDiagram
    participant User
    participant MainWindow
    participant AudioExporter
    participant Project
    participant VST3PluginInstance

    User->>MainWindow: エクスポート開始
    MainWindow->>AudioExporter: startExport(path)
    AudioExporter->>AudioExporter: レンダリング用スレッド起動
    
    loop 完了まで
        AudioExporter->>Project: 指定位置のMIDIイベントを取得
        AudioExporter->>VST3PluginInstance: processAudio (オフライン)
        AudioExporter->>AudioExporter: WAVファイルへ追記
        AudioExporter-->>MainWindow: progressChanged(float)
    end

    AudioExporter-->>MainWindow: exportFinished(bool)
    MainWindow->>User: 完了通知
```

## 10. フラッグ（マーカー）操作のデータフロー

```mermaid
sequenceDiagram
    participant User
    participant TimelineWidget
    participant Project
    participant MainWindow
    participant PlaybackController

    Note over User,TimelineWidget: フラッグ設置（長押し）
    User->>TimelineWidget: 小節部分を長押し（500ms）
    TimelineWidget->>TimelineWidget: onLongPress()
    TimelineWidget->>TimelineWidget: 小節頭にスナップ
    TimelineWidget->>Project: addFlag(tickPosition)
    Project-->>TimelineWidget: flagsChanged()
    TimelineWidget->>TimelineWidget: update() (フラッグ描画)

    Note over User,TimelineWidget: フラッグ削除（右クリック）
    User->>TimelineWidget: フラッグ付近を右クリック
    TimelineWidget->>Project: removeFlag(tickPosition)
    Project-->>TimelineWidget: flagsChanged()
    TimelineWidget->>TimelineWidget: update()

    Note over User,PlaybackController: フラッグスキップ
    User->>MainWindow: スキップボタンクリック
    MainWindow->>Project: nextFlag(currentTick) or prevFlag(currentTick)
    Project-->>MainWindow: フラッグのティック位置
    MainWindow->>PlaybackController: seekTo(flagTick)
    PlaybackController-->>MainWindow: positionChanged(flagTick)
```

## 11. オーディオファイルインポートのデータフロー

```mermaid
sequenceDiagram
    participant User
    participant ArrangementGridWidget
    participant AudioFileReader
    participant Track
    participant Clip

    User->>ArrangementGridWidget: WAV/MP3/M4Aファイルをドラッグ&ドロップ
    ArrangementGridWidget->>ArrangementGridWidget: dragEnterEvent: 拡張子チェック(.wav/.mp3/.m4a)
    ArrangementGridWidget->>ArrangementGridWidget: dropEvent → handleAudioFileDrop()
    ArrangementGridWidget->>AudioFileReader: readFile(filePath)
    
    alt WAVファイル
        AudioFileReader->>AudioFileReader: readWav() ネイティブPCM解析
    else MP3/M4A
        AudioFileReader->>AudioFileReader: readCompressedAudio() OSネイティブデコード
    end
    
    AudioFileReader->>AudioFileReader: generateWaveformPreview()
    AudioFileReader-->>ArrangementGridWidget: AudioFileData (samplesL/R, waveform)
    
    ArrangementGridWidget->>ArrangementGridWidget: オーディオ長(秒) → tick変換
    ArrangementGridWidget->>Track: addClip(dropTick, clipDuration)
    Track-->>ArrangementGridWidget: Clip*
    ArrangementGridWidget->>Clip: setAudioData(samplesL, samplesR, sampleRate, filePath)
    Clip->>Clip: ClipType = Audio, PCMデータ保持
    ArrangementGridWidget->>ArrangementGridWidget: startWaveReveal() アニメーション開始
    ArrangementGridWidget->>ArrangementGridWidget: update() (波形プレビュー描画)
```

## 12. オーディオクリップ再生時の詳細フロー

```mermaid
flowchart TD
    A[オーディオコールバック発火] --> B[AudioEngine: 利用可能フレーム数取得]
    B --> C[PlaybackController: audioRenderCallback]
    C --> D[再生位置範囲を計算]
    D --> E{各トラックをループ}
    E --> F{クリップ種別判定}
    F -->|オーディオクリップ| G[tick→秒→サンプルインデックス変換]
    G --> H[PCMデータ読み出し + 線形補間]
    H --> I[トラックバッファに加算]
    F -->|MIDIクリップ + プラグイン| J[MIDIイベント収集]
    J --> K[VST3PluginInstance::processAudio]
    K --> I
    I --> L[FXインサート処理]
    L --> M[ボリューム・パン適用]
    M --> N[フォルダバス or マスターバスへルーティング]
    N --> E
    I --> L[FXインサート処理]
    L --> M[ボリューム・パン適用]
    M --> N[フォルダバス or マスターバスへルーティング]
    N --> E
    E --> O[インターリーブして出力バッファに書込]
```

## 6. テーマ切り替えのデータフロー

```mermaid
sequenceDiagram
    participant User
    participant MainWindow
    participant ThemeManager
    participant Views

    User->>MainWindow: 電球ボタン（テーマ切り替え）クリック
    MainWindow->>ThemeManager: toggleTheme()
    ThemeManager->>ThemeManager: m_isDarkMode 反転
    ThemeManager-->>MainWindow: themeChanged()
    ThemeManager-->>Views: themeChanged()
    MainWindow->>MainWindow: 全体スタイルシート更新
    Views->>Views: setStyleSheet / update() による再描画
```
