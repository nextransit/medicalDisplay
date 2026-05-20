# macOS 医疗显示系统设计方案

**日期**: 2026-05-21
**版本**: 1.0
**状态**: 设计中

---

## 1. 项目概述

将现有的 `MetalMedicalDemo.swift` 单文件演示程序扩展为完整的 macOS 医疗显示应用，集成为期四个月的开发工作：

- **A**: macOS 应用框架 (SwiftUI + AppKit + Metal)
- **B**: AI/ML 推理引擎 (ONNX Runtime + MonoViT/MedCLIP)
- **C**: GSDF 校准模块 (交互式向导 + 手动亮度计输入)
- **D**: 调试与性能优化 (Metal Profiler + Instruments)

---

## 2. 架构概览

```
┌─────────────────────────────────────────────────────────────────┐
│                     macOS App (Swift/SwiftUI)                    │
├─────────────────────────────────────────────────────────────────┤
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐       │
│  │  UI Layer │  │  AI UI   │  │Calibration│  │ Debug    │       │
│  │          │  │  Panel   │  │  Wizard   │  │ Console  │       │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘       │
├───────┴─────────────┴─────────────┴─────────────┴──────────────┤
│                   Bridge Layer (Objective-C++)                   │
│  MedicalDisplayBridge.mm - C++ ↔ Swift 互操作                   │
├─────────────────────────────────────────────────────────────────┤
│                     SDK Core (C/C++)                             │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐            │
│  │ AI      │  │Display  │  │ GSDF    │  │ SIMD    │            │
│  │ Engine  │  │Engine   │  │Calibr.  │  │Optimize │            │
│  └─────────┘  └─────────┘  └─────────┘  └─────────┘            │
├─────────────────────────────────────────────────────────────────┤
│                     Metal GPU (Compute Shaders)                   │
│  process_frame, gsdfTransform, sobelEdge, bloodlessEnhance       │
└─────────────────────────────────────────────────────────────────┘
```

---

## 3. 子项目 A: macOS 应用框架

### 3.1 技术选型

| 组件 | 技术 | 说明 |
|------|------|------|
| UI 框架 | SwiftUI + AppKit 混合 | SwiftUI 现代 UI + AppKit 原生对话框 |
| 窗口管理 | NSWindow + SwiftUI View | NSWindow 承载 SwiftUI 内容 |
| 文件对话框 | NSOpenPanel | 原生 macOS 文件选择 |
| GPU 渲染 | Metal + MTKView | 硬件加速图像处理 |
| SDK 绑定 | Objective-C++ Bridge | C++ SDK ↔ Swift 互操作 |

### 3.2 文件结构

```
examples/macos/
├── MedicalDisplay/                    # 应用包
│   ├── main.swift                    # 入口点
│   ├── AppDelegate.swift              # 应用代理
│   ├── MainWindow.swift              # 主窗口控制器
│   ├── Views/
│   │   ├── RenderView.swift          # Metal 渲染视图
│   │   ├── ControlPanel.swift        # 控制面板
│   │   ├── AIPanel.swift             # AI 识别面板
│   │   ├── CalibrationWizard.swift   # 校准向导
│   │   └── StatusBar.swift           # 状态栏
│   ├── Bridge/
│   │   └── MedicalDisplayBridge.mm   # SDK 绑定
│   ├── Models/
│   │   └── AppState.swift            # 应用状态
│   └── Resources/
│       └── Assets.xcassets           # 资源文件
└── MetalMedicalDemo.swift            # 现有演示（保留参考）
```

### 3.3 核心功能

1. **文件菜单**
   - 加载 DICOM 文件
   - 加载图像 (PNG/JPEG/TIFF)
   - 加载视频流

2. **实时渲染**
   - Metal GPU 加速
   - 可调节参数：亮度/对比度/饱和度
   - GSDF 校准开关

3. **AI 模态显示**
   - 当前识别结果
   - 置信度百分比
   - 推荐参数

4. **多窗口支持**
   - 主显示窗口
   - 调试控制台 (可选)

---

## 4. 子项目 B: AI/ML 推理引擎

### 4.1 技术选型

| 组件 | 技术 | 来源 |
|------|------|------|
| 推理框架 | ONNX Runtime 1.17+ | Homebrew macOS 版本 |
| 模型 1 | MonoViT | HuggingFace/Monai |
| 模型 2 | MedCLIP | HuggingFace |
| 备选 | SDK 内置规则引擎 | 无模型时自动启用 |

### 4.2 模型集成方案

```bash
# 模型下载脚本
./scripts/download_models.sh
```

| 模型 | 用途 | 输入尺寸 | 输出 |
|------|------|---------|------|
| MonoViT | 医学影像分类/分割 | 224x224 RGB | 类别概率 |
| MedCLIP | 多模态医学影像 | 224x224 RGB | 类别概率 |

### 4.3 推理流程

```
图像输入 → 预处理 (resize/normalize) → ONNX 推理 → 后处理 → 模态分类
                                          ↓
                                    规则引擎回退
                                    (模型不可用时)
```

### 4.4 支持的模态

- CT (计算机断层扫描)
- MRI (磁共振成像)
- XRay (X 光片)
- Ultrasound (超声)
- PET (正电子发射计算机断层扫描)

---

## 5. 子项目 C: GSDF 校准模块

### 5.1 技术选型

| 组件 | 技术 | 说明 |
|------|------|------|
| 校准方式 | 交互式分步向导 | 用户友好界面 |
| 亮度输入 | 手动输入模式 | 通用方案 |
| GSDF 生成 | SDK `display_generate_gsdf_lut()` | DICOM Part 14 |
| 配置文件 | JSON 格式 | 便携存储 |

### 5.2 校准流程

```
Step 1: 环境设置
   ├── 选择显示器
   ├── 设置环境光传感器
   └── 确认暗室环境

Step 2: 黑电平 (L_min) 测量
   ├── 显示黑画面
   ├── 用户读取亮度计
   └── 输入 L_min 值

Step 3: 白电平 (L_max) 测量
   ├── 显示白画面 (或灰度渐变)
   ├── 用户读取亮度计
   └── 输入 L_max 值

Step 4: 生成并应用 GSDF LUT
   ├── 计算 JND 映射
   ├── 生成 3D LUT
   └── 保存配置文件
```

### 5.3 配置文件格式

```json
{
  "display_id": "built-in-display-1",
  "calibration_date": "2026-05-21",
  "luminance": {
    "black": 0.5,
    "white": 450.0
  },
  "ambient_light": 50.0,
  "gsdf_lut_path": "~/Library/Application Support/MedicalDisplay/gsdf_lut.bin",
  "delta_e": 1.8
}
```

---

## 6. 子项目 D: 调试与性能优化

### 6.1 调试工具

| 工具 | 用途 |
|------|------|
| Metal GPU Profiler | GPU 性能分析 |
| Instruments (Allocations) | 内存分配追踪 |
| Instruments (Leaks) | 内存泄漏检测 |
| Xcode Metal Shader Debugger | Shader 调试 |

### 6.2 SDK 调试日志

```cpp
// 调试宏开关
#ifdef DEBUG
    #define MEDDISP_DEBUG_LOG(...) fprintf(stderr, "[MEDDISP DEBUG] " __VA_ARGS__)
#else
    #define MEDDISP_DEBUG_LOG(...) ((void)0)
#endif
```

### 6.3 性能目标

| 分辨率 | 目标帧率 | 目标延迟 |
|--------|---------|---------|
| 1920x1080 | 60 fps | < 16.67 ms |
| 2560x1440 | 60 fps | < 16.67 ms |
| 3840x2160 (4K) | 30 fps | < 33.33 ms |

---

## 7. 实现计划

### 第一阶段: macOS 应用框架 (4 周)

1. 创建 Xcode 项目结构
2. 实现 main.swift + AppDelegate
3. 集成 Objective-C++ Bridge
4. 实现 Metal 渲染视图
5. 添加文件加载功能

### 第二阶段: AI 推理集成 (4 周)

1. 下载预训练模型
2. 集成 ONNX Runtime
3. 实现 AI 模态识别 UI
4. 添加规则引擎回退

### 第三阶段: GSDF 校准模块 (3 周)

1. 实现校准向导 UI
2. 集成 SDK GSDF 功能
3. 实现配置文件保存/加载

### 第四阶段: 调试与优化 (3 周)

1. 性能分析并优化
2. 内存泄漏修复
3. Shader 调试

**总工期: 14 周**

---

## 8. 依赖关系

```
macOS App Framework
       │
       ├── Bridge Layer (Objective-C++)
       │         │
       │         └── SDK Core (C/C++)
       │                   ├── AI Engine
       │                   ├── Display Engine
       │                   └── GSDF Module
       │
       ├── AI Inference
       │         │
       │         └── ONNX Runtime + MonoViT/MedCLIP
       │
       └── Metal GPU Shaders
                 │
                 └── process_frame, gsdfTransform, etc.
```

---

## 9. 风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| ONNX 模型不兼容 | B | 使用 SDK 规则引擎作为备选 |
| 校准精度不足 | C | 提供多级校准选项 |
| 性能不达标 | D | 分辨率自适应降级 |
| macOS Metal 版本 | A | 最低支持 macOS 12.0+ |

---

## 10. 验收标准

### A. macOS 应用
- [ ] 应用可正常启动
- [ ] 可加载 DICOM/图像文件
- [ ] Metal 渲染正常工作
- [ ] 文件菜单功能完整

### B. AI 推理
- [ ] 模型推理正常执行
- [ ] 模态识别结果合理
- [ ] 无模型时规则引擎回退正常

### C. GSDF 校准
- [ ] 校准向导可完整执行
- [ ] 生成的 GSDF LUT 正确
- [ ] 配置文件可保存/加载

### D. 调试优化
- [ ] 1080p 60fps 渲染
- [ ] 无内存泄漏
- [ ] Shader 无错误
