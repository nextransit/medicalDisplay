# AI 自适应医疗显示系统 (AI Adaptive Medical Display System)

![Version](https://img.shields.io/badge/version-v2.0-blue)
![License](https://img.shields.io/badge/license-MIT-green)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Android-orange)

## 概述

AI 自适应医疗显示系统是一个面向医疗显示器、诊断工作站、手术显示终端的智能显示控制系统。系统通过AI技术自动识别医疗影像类型，动态调整显示参数，实现"不同医疗影像自动匹配最佳显示策略"。

## 核心功能

| 模块 | 功能 | 状态 |
|------|------|------|
| **AI引擎** | 模态识别 (CT/MRI/超声/PET/DX) | ✅ |
| **显示引擎** | GSDF/HDR/色彩空间自适应 | ✅ |
| **术野增强** | 无血术野/Sobel边缘/血管增强 | ✅ |
| **GPU加速** | Vulkan/Metal Compute Shader | ✅ |
| **SIMD优化** | SSE4.2/AVX2/NEON并行加速 | ✅ |
| **多模态融合** | PET-CT/PET-MR 3D渲染 | ✅ |
| **V4L2采集** | 实时视频流处理 | ✅ |
| **联邦学习** | 隐私保护协同训练 | ✅ |
| **预测维护** | 设备健康评分/故障预测 | ✅ |

## 性能

- **SIMD流水线处理** (1920x1080):
  - 亮度/对比度: 1.47ms (681fps)
  - 全流水线: 7.42ms (135fps)
- **GPU加速** (目标):
  - Vulkan: <10ms/frame
  - Metal: <5ms/frame

## 快速开始

### 构建

```bash
# 克隆
git clone https://gitlab.com/your-org/medicalDisplay.git
cd medicalDisplay

# 创建构建目录
mkdir build && cd build

# 配置 (Linux)
cmake .. -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_VULKAN=ON \
        -DBUILD_TESTS=ON

# 构建
make -j$(nproc)

# 运行测试
ctest --output-on-failure
```

### 运行示例

```bash
# 医疗影像显示演示
./examples/linux/medical_display_demo --modality CT --frames 100

# GSDF校准演示
./examples/linux/gsdf_calibration_demo

# 术野视频增强
./examples/linux/surgical_video_demo

# 性能基准测试
./tests/test_simd_benchmark
```

## 架构

```
┌─────────────────────────────────────────────────────────────────┐
│                    AI Adaptive Medical Display System              │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────┐    ┌─────────────┐    ┌────────────────────┐   │
│  │ 输入源   │───▶│ AI识别引擎  │───▶│   自适应显示引擎    │   │
│  │         │    │             │    │                    │   │
│  │ • DICOM │    │ • 模态分类  │    │ • GSDF动态切换    │   │
│  │ • V4L2  │    │ • 特征提取  │    │ • HDR策略         │   │
│  │ • Video │    │ • ONNX推理  │    │ • 色彩空间       │   │
│  │ • RTSP  │    │ • 规则引擎  │    │ • 局部增强       │   │
│  └─────────┘    └─────────────┘    └────────────────────┘   │
│                                              │               │
│  ┌──────────────────────────────────────────┴───────────┐   │
│  │                    GPU/SIMD 加速层                 │   │
│  │  • Vulkan Compute  • Metal Compute  • SSE4.2/AVX2  │   │
│  │  • NEON (ARM)     • OpenMP并行   • 内存池        │   │
│  └─────────────────────────────────────────────────────┘   │
│                                              │               │
│  ┌──────────────────────────────────────────┴───────────┐   │
│  │                    平台抽象层                    │   │
│  │  • Linux (DRM/KMS/V4L2)  • Android (NDK)        │   │
│  │  • macOS (Metal)         • Windows (计划中)      │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

## SDK 模块

| 模块 | 路径 | 说明 |
|------|------|------|
| AI引擎 | `sdk/ai_engine/` | ONNX Runtime + 规则引擎 |
| 显示引擎 | `sdk/display_engine/` | GSDF/HDR/Vulkan |
| 术野增强 | `sdk/surgical_video/` | GPU加速视频处理 |
| SIMD优化 | `sdk/performance/` | 并行图像处理 |
| 多模态融合 | `sdk/multimodal/` | PET-CT 3D渲染 |
| 视频采集 | `sdk/platform/linux/` | V4L2接口 |
| 联邦学习 | `sdk/federated/` | FedAvg隐私训练 |
| 预测维护 | `sdk/predictive_maintenance/` | 健康评分 |

## API 使用

### SIMD 流水线处理

```c
#include "simd_processing.h"

// 配置
SimdPipelineConfig config = {
    .brightness = 0.1f,
    .contrast = 1.1f,
    .saturation = 1.2f,
    .gsdf_lut = gsdf_lut_data,
    .enable_bloodless = true,
    .blood_suppress_level = 0.5f,
    .tissue_enhance = 0.3f,
};

// 处理
uint8_t* output = process_buffer;
simd_pipeline_process(input, output, width, height, &config);
```

### GPU 管线

```c
#include "gpu_pipeline.h"

GPUPipelineConfig config = {
    .backend = GPU_BACKEND_AUTO,  // 自动选择最佳后端
    .max_width = 3840,
    .max_height = 2160,
    .enable_hdr = true,
};

GPUPipeline* pipeline;
gpu_pipeline_create(&pipeline, &config);

GPUPipelineParams params = {
    .brightness = 0.1f,
    .contrast = 1.1f,
    .saturation = 1.2f,
    .enable_bloodless = 1,
    .bloodless_strength = 0.5f,
};

gpu_pipeline_process_frame(pipeline, input, output, width, height, &params);
```

### V4L2 采集

```c
#include "v4l2_capture.h"

V4L2Capture* cap = v4l2_capture_open("/dev/video0", 1920, 1080, 
                                      V4L2_PIX_FMT_YUYV, 60);
v4l2_capture_start(cap);

uint8_t frame[1920*1080*3];
while (v4l2_capture_frame(cap, frame, sizeof(frame), 1000) == 0) {
    // 处理 frame
}

v4l2_capture_close(cap);
```

## 文档

| 文档 | 说明 |
|------|------|
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | 系统架构详细设计 |
| [API_REFERENCE.md](docs/API_REFERENCE.md) | SDK API参考 |
| [BUILD_GUIDE.md](docs/BUILD_GUIDE.md) | 构建指南 |
| [PERFORMANCE.md](docs/PERFORMANCE.md) | 性能优化报告 |
| [CALIBRATION.md](docs/CALIBRATION.md) | GSDF校准指南 |
| [SECURITY.md](docs/SECURITY.md) | 安全设计 |

## 测试

```bash
# 运行所有测试
ctest --output-on-failure

# 运行特定测试
./tests/test_simd_benchmark

# 性能基准
./tests/test_simd_benchmark 2>&1 | grep "FULL PIPELINE"
```

## 平台支持

| 平台 | GPU | SIMD | V4L2 | 状态 |
|------|-----|-------|-------|------|
| Linux x86_64 | Vulkan | SSE4.2/AVX2 | ✅ | ✅ |
| Linux ARM64 | Vulkan | NEON | ✅ | ✅ |
| macOS Apple Silicon | Metal | NEON | N/A | ✅ |
| macOS Intel | Metal | SSE4.2 | N/A | ✅ |
| Android ARM64 | Vulkan | NEON | Camera2 | 计划中 |

## 许可证

MIT License - 详见 [LICENSE](LICENSE)

## 联系方式

- GitLab: https://gitlab.com/your-org/medicalDisplay
- 邮箱: support@example.com
