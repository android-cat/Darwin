#!/bin/bash
# Darwin DAW - ビルド用スクリプト

set -e

OS_NAME="$(uname -s)"
BUILD_DIR="build"
GENERATOR="Ninja"

if [ "$OS_NAME" != "Darwin" ]; then
    # Windows (MSYS/MINGW) 向けの既定パス
    export PATH="/c/Qt/Tools/mingw1310_64/bin:/c/Qt/Tools/Ninja:$PATH"
else
    if ! command -v ninja >/dev/null 2>&1; then
        GENERATOR="Unix Makefiles"
    fi
fi

# buildディレクトリが存在しない場合は初回設定(Configure)を行う
if [ ! -d "$BUILD_DIR" ]; then
    echo "Running CMake Configure..."
    if [ "$OS_NAME" = "Darwin" ]; then
        if [ -n "$CMAKE_PREFIX_PATH" ]; then
            cmake -S . -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"
        else
            cmake -S . -B "$BUILD_DIR" -G "$GENERATOR"
        fi
    else
        cmake -S . -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_PREFIX_PATH="c:/Qt/6.10.2/mingw_64"
    fi
fi

# ビルド実行
echo "Building Darwin DAW..."
cmake --build "$BUILD_DIR" --config Release
