# AI Adaptive Medical Display System (AI自适应医疗显示系统)

基于AI的智能医疗显示系统，自动识别CT/MRI/DR/超声/病理等影像类型，动态调整GSDF/Gamma/HDR/色彩空间等显示参数，实现"不同医疗影像自动匹配最佳显示策略"。

## 核心特性

### 🎯 AI影像识别
- 支持12种影像模态自动识别
- 边缘部署，<50ms推理延迟
- NPU/GPU加速 (RK3588/Jetson Orin)
- 模态+部位联合识别

### 🖥️ 自适应显示
- DICOM GSDF动态切换
- 12-bit HDR显示支持
- 多色彩空间管理 (sRGB/DCI-P3/Rec2020)
- AI局部增强 (骨骼/肺结节/血管/细胞)

### 📋 医疗合规
- DICOM Part 14完全兼容
- IEC 60601-1-2 EMC认证
- FDA 510(k) / CE MDR支持
- 完整审计追溯

### 🔧 跨平台
- Linux: DRM/KMS + Vulkan + OpenGL ES
- Android: SurfaceFlinger + Hardware Composer
- 边缘: RK3588/RK3576/Jetson Orin

### ☁️ 云边协同
- OTA远程更新
- 联邦学习模型优化
- 预测性维护
- 多医院集中运维

## 项目结构

```
medicalDisplay/
├── sdk/
│   ├── ai_engine/          # AI影像识别引擎
│   │   ├── include/
│   │   └── src/
│   ├── display_engine/     # 自适应显示引擎
│   │   ├── include/
│   │   ├── src/
│   │   └── shaders/        # Vulkan Shaders
│   ├── dicom/              # DICOM/GSDF处理
│   │   ├── include/
│   │   └── src/
│   ├── platform/
│   │   ├── linux/          # Linux平台支持
│   │   └── android/        # Android平台支持
│   ├── cloud/              # 云边协同Agent
│   └── common/             # 公共库
├── examples/
│   └── linux/              # Linux示例程序
└── docs/                   # 文档
```

## 快速开始

### 依赖

#### Linux
```bash
# Ubuntu 22.04+
sudo apt install cmake build-essential libvulkan-dev libdrm-dev
sudo apt install libcurl4-openssl-dev libssl-dev
```

#### Android
- Android 13+
- NDK r25+
- Vulkan 1.3+

### 编译

```bash
# Linux SDK
mkdir build && cd build
cmake .. -DBUILD_PLATFORM_LINUX=ON -DENABLE_VULKAN=ON
make -j$(nproc)

# 运行示例
./examples/linux/dicom_viewer <dicom_file>
```

### macOS 当前可用构建方式

当前机器自带 `/usr/bin/c++` 的 CommandLineTools 标准库头不完整，直接用默认 Apple 工具链会在 `<cstring>` 这类基础头处失败。当前仓库已经验证可用的构建链是 Homebrew GCC 15：

```bash
cmake -S . -B build-gcc15 \
  -DBUILD_EXAMPLES=ON \
  -DBUILD_TESTS=ON \
  -DCMAKE_C_COMPILER=/opt/homebrew/bin/gcc-15 \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/bin/g++-15

cmake --build build-gcc15 -j4
ctest --test-dir build-gcc15 --output-on-failure
```

根工程示例构建也已验证：

```bash
cmake -S . -B build-gcc15-root \
  -DBUILD_EXAMPLES=ON \
  -DBUILD_TESTS=OFF \
  -DCMAKE_C_COMPILER=/opt/homebrew/bin/gcc-15 \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/bin/g++-15

cmake --build build-gcc15-root -j4
./build-gcc15-root/examples/linux/medical_display_demo --frames 2 --width 128 --height 128
./build-gcc15-root/examples/linux/gsdf_calibration_demo
./build-gcc15-root/examples/linux/cloud_demo
```

### 使用示例

```c
#include "ai_engine.h"
#include "display_engine.h"

// 1. 初始化AI引擎
AIEngineConfig ai_config = {};
ai_config.use_npu = true;
AIEngine* ai = ai_engine_create(&ai_config);

// 2. 初始化显示引擎
DisplayEngineConfig display_config = {};
display_config.use_vulkan = true;
DisplayEngine* display = display_engine_create(&display_config);

// 3. 加载影像并识别
uint16_t* dicom_data = load_dicom("ct_scan.dcm");
AIRecognitionResult result;
ai_engine_recognize_from_dicom(ai, dicom_data, 512, 512, 12, &result);

// 4. 应用AI推荐策略
display_engine_apply_strategy(display, &result.strategy);

// 5. 渲染显示
display_engine_render_dicom(display, dicom_data, 512, 512, 12, 0, 0);

// 6. 清理
ai_engine_destroy(ai);
display_engine_destroy(display);
```

## 技术文档

- [技术架构白皮书](AI_ADAPTIVE_MEDICAL_DISPLAY_SYSTEM_ARCHITECTURE.md)
- API文档 (见sdk/*/include/)
- Vulkan渲染管线 (见sdk/display_engine/shaders/)

## 认证状态

| 认证 | 状态 | 目标时间 |
|------|------|----------|
| IEC 60601-1 | 进行中 | 2026-Q3 |
| FDA 510(k) | 规划中 | 2027-Q1 |
| CE MDR | 规划中 | 2027-Q2 |

## 许可

Proprietary - 仅供授权使用

## 联系

- 技术支持: support@medical-display.ai
- 商务合作: business@medical-display.ai
