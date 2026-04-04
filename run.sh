#!/bin/bash
# Darwin DAW - 実行用スクリプト

OS_NAME="$(uname -s)"
APP_PATH="build/Darwin.exe"

if [ "$OS_NAME" = "Darwin" ]; then
    APP_PATH="build/Darwin"
fi

# ビルドディレクトリに実行ファイルがあるか確認
if [ ! -f "$APP_PATH" ]; then
    echo "Error: $APP_PATH not found."
    echo "Please run ./build.sh first."
    exit 1
fi

echo "Starting Darwin DAW..."
"./$APP_PATH"
