#!/bin/bash
# AI Medical Display — 快捷构建运行脚本
# 用法: ./run.sh [build|run|clean|all] [--debug] [--help]

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
QT_PREFIX="/opt/homebrew/opt/qt"
BUILD_TYPE="Release"
CMAKE_EXTRA_FLAGS=""

# ---- 帮助信息 ----
show_help() {
    cat <<EOF
AI Medical Display — 构建与运行脚本

用法: $0 [命令] [选项]

命令:
  build       构建项目（默认 Release，加 --debug 可切换 Debug）
  run         运行已构建的应用
  clean       清理构建目录
  all         构建并运行

选项:
  --debug     使用 Debug 构建类型（默认 Release）
  --help      显示此帮助信息

示例:
  $0                    # 构建 (Release) 并运行
  $0 build --debug      # Debug 构建
  $0 clean && $0 all    # 全新构建并运行
  $0 run                # 仅运行
EOF
    exit 0
}

# ---- 依赖检查 ----
check_dependencies() {
    local missing=()

    # 检查 CMake
    if ! command -v cmake &>/dev/null; then
        missing+=("cmake")
    fi

    # 检查 Qt6 (qmake6 或 qmake)
    local qmake_found=false
    if command -v qmake6 &>/dev/null; then
        qmake_found=true
    elif command -v qmake &>/dev/null; then
        # 检查 qmake 版本是否为 Qt6
        local qmake_ver
        qmake_ver=$(qmake -query QT_VERSION 2>/dev/null || echo "")
        if [[ "$qmake_ver" == 6.* ]]; then
            qmake_found=true
        fi
    fi

    if ! $qmake_found; then
        # 检查 Homebrew 安装的 Qt6
        if [ -d "$QT_PREFIX" ] && [ -f "$QT_PREFIX/bin/qmake" ]; then
            qmake_found=true
        else
            missing+=("qt6 (brew install qt)")
        fi
    fi

    # 检查编译器
    if ! command -v clang++ &>/dev/null && ! command -v g++ &>/dev/null; then
        missing+=("clang++ 或 g++")
    fi

    if [ ${#missing[@]} -gt 0 ]; then
        echo "❌ 缺少依赖:"
        for dep in "${missing[@]}"; do
            echo "   - $dep"
        done
        echo ""
        echo "💡 安装提示 (macOS):"
        echo "   brew install cmake qt"
        exit 1
    fi

    echo "✅ 依赖检查通过"
}

# ---- 解析参数 ----
COMMAND=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --help|-h)
            show_help
            ;;
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        build|run|clean|all)
            COMMAND="$1"
            shift
            ;;
        *)
            echo "❌ 未知参数: $1"
            echo "   使用 '$0 --help' 查看帮助"
            exit 1
            ;;
    esac
done

# 默认命令: all (构建 + 运行)
COMMAND="${COMMAND:-all}"

build() {
    check_dependencies

    echo "🔨 构建 AI Medical Display ($BUILD_TYPE)..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake .. \
        -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -G "Unix Makefiles" \
        $CMAKE_EXTRA_FLAGS
    cmake --build . --config "$BUILD_TYPE" -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
    echo "✅ 构建完成 ($BUILD_TYPE)"
}

run() {
    local app="$BUILD_DIR/MedicalDisplayApp.app/Contents/MacOS/MedicalDisplayApp"
    if [ ! -f "$app" ]; then
        echo "❌ 未找到构建产物，请先运行: $0 build"
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

export BUILD_TYPE

case "$COMMAND" in
    build) build ;;
    run)   run ;;
    clean) clean ;;
    all)   build && run ;;
    *)     echo "用法: $0 {build|run|clean|all} [--debug] [--help]" ;;
esac
