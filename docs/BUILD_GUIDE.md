# 构建指南 / Build Guide

本文档介绍如何构建 AI Adaptive Medical Display System SDK。

## 系统要求 / System Requirements

### Linux 构建环境

- **GCC/Clang**: C++17 支持
- **CMake**: 3.20+
- **Vulkan SDK**: 1.3+
- **Python**: 3.8+ (用于云端服务)

### Android 构建环境

- **Android NDK**: r23b+
- **Android SDK**: API 26+
- **Gradle**: 7.4+

### 依赖项 / Dependencies

#### Linux

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libvulkan-dev \
    libdrm-dev \
    libgbm-dev \
    libwayland-dev \
    wayland-protocols \
    libssl-dev \
    zlib1g-dev \
    libmosquitto-dev
```

#### Android

```bash
# 使用 sdkmanager 安装 NDK
export ANDROID_NDK_ROOT=$ANDROID_HOME/ndk/23.0.7599859
```

## 构建步骤 / Build Steps

### Linux 构建

```bash
# 克隆项目
cd /path/to/medicalDisplay

# 创建构建目录
mkdir build && cd build

# 配置 CMake
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DBUILD_EXAMPLES=ON \
    -DBUILD_TESTS=ON

# 编译
make -j$(nproc)

# 运行测试
ctest --output-on-failure

# 安装 (可选)
sudo make install
```

### Android 构建

```bash
# 配置 Android SDK 和 NDK
export ANDROID_NDK_HOME=/path/to/android-ndk
export ANDROID_SDK_ROOT=/path/to/android-sdk

# 创建构建目录
mkdir android_build && cd android_build

# 配置 CMake
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-26 \
    -DCMAKE_BUILD_TYPE=Release

# 编译
make -j$(nproc)

# 生成 AAR
./gradlew assembleRelease
```

### 云端服务构建

```bash
cd cloud_server

# 创建虚拟环境
python3 -m venv venv
source venv/bin/activate

# 安装依赖
pip install -r requirements.txt

# 运行测试
pytest tests/

# 启动服务
python src/main.py
```

## 构建选项 / Build Options

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `BUILD_EXAMPLES` | 构建示例程序 | ON |
| `BUILD_TESTS` | 构建单元测试 | ON |
| `ENABLE_VULKAN` | 启用 Vulkan 支持 | ON |
| `ENABLE_DRM` | 启用 DRM/KMS 支持 | ON |
| `ENABLE_MQTT` | 启用 MQTT 支持 | AUTO |

## 常见问题 / FAQ

### 1. Vulkan SDK 未找到

```bash
# 手动指定 Vulkan 路径
cmake .. -DVulkan_INCLUDE_DIR=/path/to/vulkan/include -DVulkan_LIBRARY=/path/to/libvulkan.so
```

### 2. DRM 头文件缺失

```bash
sudo apt-get install libdrm-dev
```

### 3. NPU 运行时库未找到

确保 RKNN SDK 已正确安装:
```bash
export LD_LIBRARY_PATH=/path/to/rknn/runtime:$LD_LIBRARY_PATH
```

## 验证构建

```bash
# 验证库文件
ls -la build/lib/*.so

# 验证示例程序
./build/examples/medical_display_demo --help
```
