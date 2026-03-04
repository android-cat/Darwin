# Darwin DAW - コーディング規約

## 1. 命名規則

### 1.1 クラス名
- **PascalCase**を使用
- 明確で説明的な名前

```cpp
// Good
class MixerChannelWidget;
class PlaybackController;
class PianoRollGridWidget;

// Bad
class mixerChannel;
class playback_controller;
class PRGrid;
```

### 1.2 メンバ変数
- **m_** プレフィックス + **camelCase**

```cpp
class Track {
private:
    QString m_name;
    double m_volume;
    bool m_isMuted;
    QList<Clip*> m_clips;
};
```

### 1.3 メソッド名
- **camelCase**を使用
- 動詞で始める（getter除く）

```cpp
// Getter
QString name() const;
double volume() const;
bool isMuted() const;

// Setter
void setName(const QString& name);
void setVolume(double volume);
void setMuted(bool muted);

// Actions
void addClip(Clip* clip);
void removeClip(Clip* clip);
void updateDisplay();
```

### 1.4 シグナル名
- **過去形**または**状態変化**を表す

```cpp
signals:
    void trackAdded(Track* track);
    void clipRemoved(Clip* clip);
    void playheadChanged(qint64 position);
    void selectionChanged();
```

### 1.5 スロット名
- **on** + シグナル発行元 + シグナル名、または動作を表す名前

```cpp
private slots:
    void onPlayButtonClicked();
    void onTrackAdded(Track* track);
    void updatePlayhead(qint64 position);
```

### 1.6 定数
- **ALL_CAPS** + アンダースコア

```cpp
const int DEFAULT_BPM = 120;
const int TICKS_PER_BEAT = 480;
const QColor ACCENT_COLOR = QColor("#FF3366");
```

## 2. ファイル構成

### 2.1 ディレクトリ構造

```
src/
├── main.cpp
├── MainWindow.cpp / .h
├── audio/              # オーディオエンジン (WASAPI), エクスポート
├── commands/           # Undo/Redo コマンド群
├── common/             # 共通定数, ユーティリティ (MidiFileParser, ChordDetector 等)
├── controllers/        # PlaybackController 等
├── models/             # Project, Track, Clip, Note
├── plugins/            # VST3PluginInstance, VST3Scanner
├── views/
│   ├── arrangement/    # ArrangementView, ArrangementGridWidget, TimelineWidget
│   ├── pianoroll/      # PianoRollView, PianoRollGridWidget, VelocityLaneWidget
│   ├── compose/        # ComposeView
│   ├── mix/            # MixView
│   ├── source/         # SourceView, ScanSpinnerWidget, PluginLoadOverlay
│   └── plugineditor/   # PluginEditorWidget
└── widgets/            # FaderWidget, KnobWidget, MixerChannelWidget, LevelMeterWidget 等
```

- Viewsはドメインごとにサブフォルダに分類する
- CMakeLists.txtで各サブフォルダを `include_directories` に追加済み

### 2.5 大規模クラスの分割ファイル規約

1つのクラスが大きくなった場合（概ね500行超）、実装を複数の `.cpp` に分割する。
ヘッダー（`.h`）は分割せず1つのまま保持し、実装のみを機能単位で分離する。

**命名規則**: `<ClassName>_<Category>.cpp`

```
# 例: ArrangementGridWidget の分割
ArrangementGridWidget.h              # 全メンバ宣言（変更なし）
ArrangementGridWidget.cpp            # コンストラクタ、セットアップ、ユーティリティ
ArrangementGridWidget_Painting.cpp   # paintEvent、描画ヘルパー
ArrangementGridWidget_Input.cpp      # マウス/キーイベントハンドラ
ArrangementGridWidget_DragDrop.cpp   # ドラッグ&ドロップ処理
ArrangementGridWidget_Animation.cpp  # アニメーション関連
```

**ルール**:
- 各分割ファイルは同じヘッダー（`#include "ClassName.h"`）をインクルードする
- `static` ヘルパー関数が複数ファイルで必要な場合、各ファイルに同じ定義を置く（internal linkage）
- 分割ファイルを新規作成したら `CMakeLists.txt` の `add_executable()` に追加すること
- ファイル先頭に `@file` / `@brief` コメントで内容を明示する

### 2.2 ヘッダーファイル (.h)

```cpp
#pragma once

#include <QtWidgets>  // Qt includes first

#include "Project.h"  // Project includes second

class ClassName : public QWidget
{
    Q_OBJECT

public:
    explicit ClassName(QWidget* parent = nullptr);
    ~ClassName() override;

    // Getters
    QString name() const;
    
    // Setters
    void setName(const QString& name);

signals:
    void nameChanged(const QString& name);

public slots:
    void updateDisplay();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setupUi();
    
    QString m_name;
    QLabel* m_label;
};
```

### 2.3 ソースファイル (.cpp)

```cpp
#include "ClassName.h"

#include <QDebug>  // Additional includes after main header

ClassName::ClassName(QWidget* parent)
    : QWidget(parent)
    , m_name()
    , m_label(nullptr)
{
    setupUi();
}

ClassName::~ClassName() = default;

void ClassName::setupUi()
{
    // Implementation
}

QString ClassName::name() const
{
    return m_name;
}

void ClassName::setName(const QString& name)
{
    if (m_name != name) {
        m_name = name;
        emit nameChanged(m_name);
    }
}
```

### 2.4 オーディオスレッドのコーディング規約

- オーディオレンダリングコールバック内では **メモリアロケーション禁止**（`new`, `malloc`, `QList::append` 等）
- スレッド間共有変数には `std::atomic` を使用
- COM インターフェースはヘッダーではなく `.cpp` にのみ `#include` する
- WASAPI GUID は `__uuidof()` を使用する（MSVC 組み込み。`INITGUID` / `IID_*` / `CLSID_*` マクロ不要）

```cpp
// Good - AudioEngine.cpp（MSVC / __uuidof を使用）
#include <mmdeviceapi.h>
#include <audioclient.h>

hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                      __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));

// Bad - INITGUID マクロを使うと他の翻訳単位で FOLDERID_* が未宣言になる
// #define INITGUID
// #include <initguid.h>
// hr = CoCreateInstance(CLSID_MMDeviceEnumerator, ...);
```

## 3. Qt固有の規約

### 3.1 親子関係
- ウィジェットは親を指定してメモリ管理を委譲

```cpp
// Good - 親を指定
QLabel* label = new QLabel("Text", this);

// Bad - 親なし（メモリリークの可能性）
QLabel* label = new QLabel("Text");
```

### 3.2 シグナル/スロット接続
- **新しい構文**を使用（コンパイル時チェック）

```cpp
// Good
connect(button, &QPushButton::clicked, this, &MyClass::onButtonClicked);

// Avoid
connect(button, SIGNAL(clicked()), this, SLOT(onButtonClicked()));
```

### 3.3 スタイルシート
- 複数行のスタイルシートは `R"(...)` を使用

```cpp
widget->setStyleSheet(R"(
    QWidget {
        background-color: #ffffff;
        border: 1px solid #e2e8f0;
        border-radius: 8px;
    }
    QWidget:hover {
        border-color: #FF3366;
    }
)");
```

## 4. コードフォーマット

### 4.1 インデント
- **4スペース**（タブ禁止）

### 4.2 ブレース
- 同じ行に開きブレース

```cpp
if (condition) {
    // code
} else {
    // code
}

void function()
{
    // function body
}
```

### 4.3 行の長さ
- **100文字**以内を推奨

### 4.4 空行
- 論理的なブロック間に1行の空行
- 関数間に1行の空行

## 5. コメント

### 5.1 ファイルヘッダー
```cpp
/**
 * @file ClassName.cpp
 * @brief 簡潔な説明
 */
```

### 5.2 関数コメント
- 複雑なロジックにのみ追加
- 「何を」ではなく「なぜ」を説明

```cpp
// Bad
// ループで配列を処理
for (int i = 0; i < count; ++i) { ... }

// Good
// 逆順で処理することで削除時のインデックスずれを防ぐ
for (int i = count - 1; i >= 0; --i) { ... }
```

## 6. エラー処理

### 6.1 nullチェック
```cpp
if (track == nullptr) {
    qWarning() << "Track is null";
    return;
}
```

### 6.2 範囲チェック
```cpp
if (index < 0 || index >= m_tracks.size()) {
    qWarning() << "Index out of range:" << index;
    return nullptr;
}
```
