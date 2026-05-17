#!/bin/bash
# AI Medical Display — 快捷构建运行脚本
# 用法: ./run.sh [build|run|clean]

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
QT_PREFIX="/opt/homebrew/opt/qt"

build() {
    echo "🔨 构建 AI Medical Display..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake .. \
        -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
        -DCMAKE_BUILD_TYPE=Release \
        -G "Unix Makefiles"
    cmake --build . --config Release -j$(sysctl -n hw.ncpu)
    echo "✅ 构建完成"
}

run() {
    local app="$BUILD_DIR/MedicalDisplayApp.app/Contents/MacOS/MedicalDisplayApp"
    if [ ! -f "$app" ]; then
        echo "❌ 未找到构建产物，请先运行 ./run.sh build"
        exit 1
    fi
    echo "🚀 启动 AI Medical Display..."
    "$app" &
    echo "✅ PID: $!"
}

clean() {
    echo "🧹 清理构建目录..."
    rm -rf "$BUILD_DIR"
    echo "✅ 清理完成"
}

case "${1:-run}" in
    build) build ;;
    run)   run ;;
    clean) clean ;;
    all)   build && run ;;
    *)     echo "用法: $0 {build|run|clean|all}" ;;
esac
