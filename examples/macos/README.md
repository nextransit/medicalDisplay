# MedicalDisplay macOS 应用

AI 医疗显示系统的 macOS 原生应用。

## 构建要求

- macOS 12.0+
- Xcode 15.0+
- XcodeGen: `brew install xcodegen`

## 生成 Xcode 项目

```bash
cd examples/macos
xcodegen generate
```

## 构建项目

```bash
# 使用 Xcode
open MedicalDisplay.xcodeproj

# 或使用命令行
xcodebuild -project MedicalDisplay.xcodeproj -scheme MedicalDisplay -configuration Debug build
```

## 功能

- [x] Metal GPU 加速渲染
- [x] AI 模态识别 (CT/MRI/XRay/超声/PET)
- [x] GSDF DICOM Part 14 校准
- [x] Sobel 边缘检测
- [x] 无血术野增强
- [x] 窗口/级别预设
- [x] DICOM 文件支持
- [x] 多帧导航

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| Cmd+O | 打开图像 |
| Cmd+Shift+O | 打开 DICOM |
| Cmd+W | 关闭 |
| Cmd+F | 全屏 |
| Cmd+1 | 显示/隐藏控制面板 |
| Cmd+2 | 显示/隐藏 AI 面板 |

## 项目结构

```
MedicalDisplay/
├── main.swift              # 应用入口
├── AppDelegate.swift       # 应用代理
├── MetalRenderer.swift    # Metal 渲染器
├── Views/
│   ├── ContentView.swift      # 主布局
│   ├── RenderView.swift       # MTKView 封装
│   ├── ControlPanel.swift     # 控制面板
│   ├── AIPanel.swift         # AI 面板
│   ├── StatusBar.swift        # 状态栏
│   ├── CalibrationWizard.swift # 校准向导
│   └── DicomNavigatorView.swift # DICOM 导航
├── Bridge/
│   ├── AIEngineBridge.swift   # AI 引擎桥接
│   ├── MedicalDisplayBridge.h # ObjC 桥接头
│   └── MedicalDisplayBridge.m # ObjC 桥接实现
├── Models/
│   ├── AppState.swift          # 应用状态
│   └── CalibrationProfile.swift # 校准配置
└── Resources/
    └── Assets.xcassets
```

## 依赖

- Metal (系统框架)
- MetalKit (系统框架)
- simd (系统框架)
- ONNX Runtime (可选，用于 AI 推理)
