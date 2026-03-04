#!/bin/bash
# Darwin DAW - ビルド用スクリプト

# QtとNinjaのパスを通す
export PATH="/c/Qt/Tools/mingw1310_64/bin:/c/Qt/Tools/Ninja:$PATH"

# buildディレクトリが存在しない場合は初回設定(Configure)を行う
if [ ! -d "build" ]; then
    echo "Running CMake Configure..."
    cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="c:/Qt/6.10.2/mingw_64"
fi

# ビルド実行
echo "Building Darwin DAW..."
cmake --build build
