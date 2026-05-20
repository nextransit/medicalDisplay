# macOS 医疗显示系统实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `MetalMedicalDemo.swift` 扩展为完整的 macOS 医疗显示应用，支持 AI 模态识别、GSDF 校准和性能优化

**Architecture:** SwiftUI + AppKit 混合架构，Objective-C++ Bridge 连接 C++ SDK，Metal GPU 加速渲染。四个子项目依次开发：应用框架(A) → AI推理(B) + 校准(C) → 调试优化(D)。

**Tech Stack:** SwiftUI, AppKit, Metal, MTKView, ONNX Runtime, Objective-C++

---

## 文件结构

```
examples/macos/MedicalDisplay/
├── main.swift                    # 应用入口
├── AppDelegate.swift             # 应用代理
├── MainWindow.swift             # 主窗口
├── Views/
│   ├── RenderView.swift         # Metal 渲染视图
│   ├── ControlPanel.swift       # 控制面板
│   ├── AIPanel.swift            # AI 识别面板
│   ├── CalibrationWizard.swift  # 校准向导
│   └── StatusBar.swift         # 状态栏
├── Bridge/
│   └── MedicalDisplayBridge.mm  # SDK 绑定 (已存在)
├── Models/
│   └── AppState.swift          # 应用状态
└── Resources/
    └── Assets.xcassets         # 资源文件

sdk/ai_engine/models/            # AI 模型目录
├── monovit.onnx                # 预训练模型
└── medclip.onnx
```

---

## Phase 1: macOS 应用框架 (A)

### Task 1: 创建项目基础结构

**Files:**
- Create: `examples/macos/MedicalDisplay/main.swift`
- Create: `examples/macos/MedicalDisplay/AppDelegate.swift`
- Create: `examples/macos/MedicalDisplay/Resources/Assets.xcassets/`

- [ ] **Step 1: 创建 main.swift**

```swift
import AppKit

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.run()
```

- [ ] **Step 2: 创建 AppDelegate.swift**

```swift
import AppKit
import SwiftUI

class AppDelegate: NSObject, NSApplicationDelegate {
    var window: NSWindow!

    func applicationDidFinishLaunching(_ notification: Notification) {
        let contentView = ContentView()

        window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 1280, height: 800),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.center()
        window.setFrameAutosaveName("MedicalDisplayMainWindow")
        window.contentView = NSHostingView(rootView: contentView)
        window.title = "AI Medical Display"
        window.makeKeyAndOrderFront(nil)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return true
    }
}
```

- [ ] **Step 3: 创建 Assets.xcassets 目录结构**

```bash
mkdir -p examples/macos/MedicalDisplay/Resources/Assets.xcassets/AppIcon.appiconset
cat > examples/macos/MedicalDisplay/Resources/Assets.xcassets/Contents.json << 'EOF'
{
  "info" : {
    "author" : "xcode",
    "version" : 1
  }
}
EOF
```

- [ ] **Step 4: 提交**

```bash
git add examples/macos/MedicalDisplay/main.swift examples/macos/MedicalDisplay/AppDelegate.swift
git commit -m "feat(macos): add app entry point and AppDelegate"
```

---

### Task 2: 创建 ContentView 主布局

**Files:**
- Create: `examples/macos/MedicalDisplay/Views/ContentView.swift`
- Create: `examples/macos/MedicalDisplay/Models/AppState.swift`

- [ ] **Step 1: 创建 AppState.swift**

```swift
import Foundation
import Combine

class AppState: ObservableObject {
    @Published var currentModality: String = "Unknown"
    @Published var confidence: Float = 0.0
    @Published var brightness: Float = 0.0
    @Published var contrast: Float = 1.0
    @Published var saturation: Float = 1.0
    @Published var enableGsdf: Bool = true
    @Published var enableBloodless: Bool = false
    @Published var isProcessing: Bool = false
    @Published var currentImagePath: String?

    let aiEngine = AIEngineBridge()
}
```

- [ ] **Step 2: 创建 ContentView.swift**

```swift
import SwiftUI

struct ContentView: View {
    @StateObject private var appState = AppState()

    var body: some View {
        HSplitView {
            // 左侧控制面板
            ControlPanel(appState: appState)
                .frame(minWidth: 280, maxWidth: 320)

            // 中间渲染区域
            VStack(spacing: 0) {
                RenderView(appState: appState)
                StatusBar(appState: appState)
            }
        }
    }
}
```

- [ ] **Step 3: 提交**

```bash
git add examples/macos/MedicalDisplay/Views/ContentView.swift examples/macos/MedicalDisplay/Models/AppState.swift
git commit -m "feat(macos): add ContentView layout and AppState"
```

---

### Task 3: 实现 RenderView (Metal 渲染)

**Files:**
- Create: `examples/macos/MedicalDisplay/Views/RenderView.swift`
- Modify: `examples/macos/MedicalDisplay/Bridge/MedicalDisplayBridge.mm` (扩展现有文件)

- [ ] **Step 1: 创建 RenderView.swift**

```swift
import SwiftUI
import MetalKit

struct RenderView: NSViewRepresentable {
    @ObservedObject var appState: AppState

    func makeNSView(context: Context) -> MTKView {
        let mtkView = MTKView()
        mtkView.device = MTLCreateSystemDefaultDevice()
        mtkView.delegate = context.coordinator
        mtkView.enableSetNeedsDisplay = true
        mtkView.isPaused = false
        return mtkView
    }

    func updateNSView(_ nsView: MTKView, context: Context) {
        context.coordinator.appState = appState
    }

    func makeCoordinator() -> Coordinator {
        Coordinator(appState: appState)
    }

    class Coordinator: NSObject, MTKViewDelegate {
        var appState: AppState
        var renderer: MetalRenderer?

        init(appState: AppState) {
            self.appState = appState
            self.renderer = MetalRenderer()
        }

        func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

        func draw(in view: MTKView) {
            renderer?.render(appState: appState, in: view)
        }
    }
}
```

- [ ] **Step 2: 创建 MetalRenderer.swift**

```swift
import Metal
import MetalKit
import simd

class MetalRenderer {
    let device: MTLDevice
    let commandQueue: MTLCommandQueue
    let pipelineState: MTLComputePipelineState

    init?() {
        guard let device = MTLCreateSystemDefaultDevice(),
              let queue = device.makeCommandQueue() else {
            return nil
        }
        self.device = device
        self.commandQueue = queue

        // 使用内置 compute shader (来自 MetalMedicalDemo.swift)
        let shaderSource = """
        #include <metal_stdlib>
        using namespace metal;

        struct PipelineParams {
            uint width;
            uint height;
            float brightness;
            float contrast;
            float saturation;
            uint enableGsdf;
            uint enableBloodless;
            float bloodSuppress;
            float tissueEnhance;
            uint enableSobel;
            float sobelThreshold;
            uint mode;
        };

        float rgb2gray(float3 c) {
            return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
        }

        float gsdfTransform(float gray, uint enable) {
            if (enable == 0) return gray;
            float jnd = gray * 8.5;
            float luminance = pow(10.0, -0.6225 + 0.0820 * log(jnd) / log(10.0));
            return clamp(luminance / 40.0, 0.0, 1.0);
        }

        kernel void process_frame(texture2d<float, access::read> input [[texture(0)]],
                                  texture2d<float, access::write> output [[texture(1)]],
                                  constant PipelineParams& params [[buffer(0)]],
                                  uint2 gid [[thread_position_in_grid]]) {
            if (gid.x >= params.width || gid.y >= params.height) return;
            float4 pixel = input.read(gid);
            float3 rgb = pixel.rgb;
            if (params.brightness != 0.0 || params.contrast != 1.0) {
                rgb = (rgb - 0.5) * params.contrast + 0.5 + params.brightness;
                rgb = clamp(rgb, 0.0, 1.0);
            }
            if (params.enableGsdf == 1) {
                float gray = rgb2gray(rgb);
                float gsdf_val = gsdfTransform(gray, 1);
                rgb *= (gsdf_val / max(gray, 0.001));
                rgb = clamp(rgb, 0.0, 1.0);
            }
            output.write(float4(rgb, 1.0), gid);
        }
        """

        guard let library = try? device.makeLibrary(source: shaderSource, options: nil),
              let function = library.makeFunction(name: "process_frame") else {
            return nil
        }

        do {
            self.pipelineState = try device.makeComputePipelineState(function: function)
        } catch {
            print("Shader compile error: \(error)")
            return nil
        }
    }

    func render(appState: AppState, in view: MTKView) {
        // 实现渲染逻辑
    }
}
```

- [ ] **Step 3: 提交**

```bash
git add examples/macos/MedicalDisplay/Views/RenderView.swift
git commit -m "feat(macos): add Metal render view"
```

---

### Task 4: 实现 ControlPanel (控制面板)

**Files:**
- Create: `examples/macos/MedicalDisplay/Views/ControlPanel.swift`

- [ ] **Step 1: 创建 ControlPanel.swift**

```swift
import SwiftUI

struct ControlPanel: View {
    @ObservedObject var appState: AppState

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            // 文件加载
            GroupBox("文件") {
                Button("加载图像...") {
                    openImage()
                }
                Button("加载 DICOM...") {
                    openDicom()
                }
            }

            // 显示参数
            GroupBox("显示参数") {
                VStack(alignment: .leading) {
                    Text("亮度: \(String(format: "%.2f", appState.brightness))")
                    Slider(value: $appState.brightness, in: -1...1)

                    Text("对比度: \(String(format: "%.2f", appState.contrast))")
                    Slider(value: $appState.contrast, in: 0.5...2.0)

                    Text("饱和度: \(String(format: "%.2f", appState.saturation))")
                    Slider(value: $appState.saturation, in: 0...2.0)
                }
            }

            // 功能开关
            GroupBox("功能") {
                Toggle("启用 GSDF", isOn: $appState.enableGsdf)
                Toggle("无血术野增强", isOn: $appState.enableBloodless)
            }

            Spacer()
        }
        .padding()
        .frame(minWidth: 280)
    }

    func openImage() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.image, .png, .jpeg, .tiff]
        if panel.runModal() == .OK {
            appState.currentImagePath = panel.url?.path
        }
    }

    func openDicom() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.data]
        if panel.runModal() == .OK {
            appState.currentImagePath = panel.url?.path
        }
    }
}
```

- [ ] **Step 2: 提交**

```bash
git add examples/macos/MedicalDisplay/Views/ControlPanel.swift
git commit -m "feat(macos): add control panel with sliders"
```

---

### Task 5: 实现 StatusBar (状态栏)

**Files:**
- Create: `examples/macos/MedicalDisplay/Views/StatusBar.swift`

- [ ] **Step 1: 创建 StatusBar.swift**

```swift
import SwiftUI

struct StatusBar: View {
    @ObservedObject var appState: AppState

    var body: some View {
        HStack {
            if let path = appState.currentImagePath {
                Text("文件: \(URL(fileURLWithPath: path).lastPathComponent)")
            } else {
                Text("无图像")
            }

            Spacer()

            if appState.isProcessing {
                ProgressView()
                    .scaleEffect(0.7)
                Text("处理中...")
            }

            Text("模态: \(appState.currentModality)")
                .foregroundColor(.secondary)
        }
        .padding(.horizontal)
        .frame(height: 24)
        .background(Color(NSColor.windowBackgroundColor))
    }
}
```

- [ ] **Step 2: 提交**

```bash
git add examples/macos/MedicalDisplay/Views/StatusBar.swift
git commit -m "feat(macos): add status bar"
```

---

## Phase 2: AI 推理引擎 (B)

### Task 6: 下载预训练模型

**Files:**
- Create: `scripts/download_models.sh`
- Create: `sdk/ai_engine/models/README.md`

- [ ] **Step 1: 创建下载脚本**

```bash
#!/bin/bash
# scripts/download_models.sh

set -e

MODEL_DIR="$(cd "$(dirname "$0")/../sdk/ai_engine/models" && pwd)"
mkdir -p "$MODEL_DIR"

echo "下载 MonoViT 模型..."
# 使用 Monai 模型仓库 (示例 URL，需替换为实际可用 URL)
curl -L -o "$MODEL_DIR/monovit.onnx" \
    "https://github.com/Project-MONAI/model-zoo/releases/download/monovit.onnx"

echo "下载 MedCLIP 模型..."
curl -L -o "$MODEL_DIR/medclip.onnx" \
    "https://huggingface.co/medclip/medclip.onnx"

echo "模型下载完成: $MODEL_DIR"
ls -la "$MODEL_DIR"
```

- [ ] **Step 2: 创建模型目录 README**

```markdown
# AI 模型目录

下载模型请运行: `./scripts/download_models.sh`

## 模型说明

| 模型 | 用途 | 大小 | 备注 |
|------|------|------|------|
| monovit.onnx | 医学影像分类 | ~100MB | MonoViT-base |
| medclip.onnx | 多模态分类 | ~200MB | MedCLIP |
```

- [ ] **Step 3: 提交**

```bash
git add scripts/download_models.sh sdk/ai_engine/models/README.md
git commit -m "feat(ai): add model download script"
```

---

### Task 7: 集成 ONNX Runtime

**Files:**
- Create: `examples/macos/MedicalDisplay/Bridge/AIEngineBridge.swift`
- Modify: `examples/macos/MedicalDisplay/Bridge/MedicalDisplayBridge.mm` (添加 ONNX 调用)

- [ ] **Step 1: 创建 AIEngineBridge.swift**

```swift
import Foundation

class AIEngineBridge {
    private var onnxBackend: OpaquePointer?

    init() {
        // 初始化 ONNX Runtime
        setupONNXBackend()
    }

    private func setupONNXBackend() {
        // 从 SDK 获取 ONNX 后端状态
        // 实际通过 Bridge 调用 C++ SDK
    }

    func recognize(imageData: [UInt8], width: Int, height: Int) -> (modality: String, confidence: Float) {
        // 调用 SDK ai_engine_recognize_from_image
        return ("CT", 0.91)
    }

    deinit {
        // 清理资源
    }
}
```

- [ ] **Step 2: 扩展 Bridge 头文件**

```objc
// MedicalDisplayBridge.h
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface AIEngineBridge : NSObject
- (nullable instancetype)init;
- (NSDictionary *)recognizeFromImage:(NSData *)imageData width:(int)width height:(int)height;
@end

NS_ASSUME_NONNULL_END
```

- [ ] **Step 3: 提交**

```bash
git add examples/macos/MedicalDisplay/Bridge/AIEngineBridge.swift examples/macos/MedicalDisplay/Bridge/MedicalDisplayBridge.m
git commit -m "feat(ai): add AI engine bridge"
```

---

### Task 8: 实现 AIPanel (AI 面板)

**Files:**
- Create: `examples/macos/MedicalDisplay/Views/AIPanel.swift`

- [ ] **Step 1: 创建 AIPanel.swift**

```swift
import SwiftUI

struct AIPanel: View {
    @ObservedObject var appState: AppState

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("AI 模态识别")
                .font(.headline)

            if appState.isProcessing {
                ProgressView()
                Text("分析中...")
            } else {
                HStack {
                    Text("模态:")
                    Text(appState.currentModality)
                        .fontWeight(.bold)
                }

                HStack {
                    Text("置信度:")
                    Text("\(Int(appState.confidence * 100))%")
                }

                Divider()

                Text("推荐参数")
                    .font(.subheadline)
                    .foregroundColor(.secondary)

                Text("亮度: \(String(format: "%.2f", recommendedBrightness))")
                Text("对比度: \(String(format: "%.2f", recommendedContrast))")
                Text("饱和度: \(String(format: "%.2f", recommendedSaturation))")

                Button("应用推荐参数") {
                    applyRecommendedParams()
                }
            }
        }
        .padding()
    }

    private var recommendedBrightness: Float {
        switch appState.currentModality {
        case "CT": return 0.05
        case "MRI": return 0.1
        case "XRay": return 0.0
        case "Ultrasound": return 0.15
        default: return 0.0
        }
    }

    private var recommendedContrast: Float {
        switch appState.currentModality {
        case "CT": return 1.15
        case "MRI": return 1.2
        case "XRay": return 1.1
        case "Ultrasound": return 1.0
        default: return 1.0
        }
    }

    private var recommendedSaturation: Float {
        return 1.0
    }

    func applyRecommendedParams() {
        appState.brightness = recommendedBrightness
        appState.contrast = recommendedContrast
        appState.saturation = recommendedSaturation
    }
}
```

- [ ] **Step 2: 提交**

```bash
git add examples/macos/MedicalDisplay/Views/AIPanel.swift
git commit -m "feat(ai): add AI panel UI"
```

---

## Phase 3: GSDF 校准模块 (C)

### Task 9: 实现 CalibrationWizard (校准向导)

**Files:**
- Create: `examples/macos/MedicalDisplay/Views/CalibrationWizard.swift`

- [ ] **Step 1: 创建 CalibrationWizard.swift**

```swift
import SwiftUI

struct CalibrationWizard: View {
    @State private var currentStep = 1
    @State private var blackLuminance: String = ""
    @State private var whiteLuminance: String = ""
    @State private var ambientLight: String = ""
    @State private var isCalibrating = false
    @State private var calibrationComplete = false

    var body: some View {
        VStack(spacing: 20) {
            Text("GSDF 校准向导")
                .font(.title)

            // 步骤指示器
            HStack {
                ForEach(1...4, id: \.self) { step in
                    Circle()
                        .fill(step <= currentStep ? Color.blue : Color.gray)
                        .frame(width: 30, height: 30)
                        .overlay(Text("\(step)"))
                    if step < 4 {
                        Rectangle()
                            .fill(step < currentStep ? Color.blue : Color.gray)
                            .frame(height: 2)
                    }
                }
            }
            .padding()

            // 步骤内容
            Group {
                switch currentStep {
                case 1:
                    Step1Environment()
                case 2:
                    Step2BlackLevel(input: $blackLuminance)
                case 3:
                    Step3WhiteLevel(input: $whiteLuminance, ambient: $ambientLight)
                case 4:
                    Step4GenerateGSDF(isCalibrating: $isCalibrating, complete: $calibrationComplete)
                default:
                    EmptyView()
                }
            }

            // 导航按钮
            HStack {
                if currentStep > 1 {
                    Button("上一步") {
                        currentStep -= 1
                    }
                }
                Spacer()
                if currentStep < 4 {
                    Button("下一步") {
                        currentStep += 1
                    }
                    .disabled(!canProceed)
                } else if calibrationComplete {
                    Button("完成") {
                        // 关闭向导
                    }
                }
            }
        }
        .padding()
    }

    private var canProceed: Bool {
        switch currentStep {
        case 2: return Double(blackLuminance) != nil
        case 3: return Double(whiteLuminance) != nil && Double(ambientLight) != nil
        default: return true
        }
    }
}

struct Step1Environment: View {
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("步骤 1: 环境设置")
                .font(.headline)

            Text("请确保：")
            Text("• 显示器已预热至少 30 分钟")
            Text("• 环境光传感器已连接")
            Text("• 处于暗室环境")

            Spacer()
        }
    }
}

struct Step2BlackLevel: View {
    @Binding var input: String

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("步骤 2: 黑电平测量")
                .font(.headline)

            Text("显示全黑画面，读取亮度计数值：")

            TextField("黑电平亮度 (cd/m²)", text: $input)
                .textFieldStyle(.roundedBorder)

            Text("提示：典型值为 0.5-1.5 cd/m²")
                .foregroundColor(.secondary)

            Spacer()
        }
    }
}

struct Step3WhiteLevel: View {
    @Binding var input: String
    @Binding var ambient: String

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("步骤 3: 白电平测量")
                .font(.headline)

            Text("显示全白画面，读取亮度计数值：")

            TextField("白电平亮度 (cd/m²)", text: $input)
                .textFieldStyle(.roundedBorder)

            Text("环境光照 (lux):")
            TextField("环境光照", text: $ambient)
                .textFieldStyle(.roundedBorder)

            Spacer()
        }
    }
}

struct Step4GenerateGSDF: View {
    @Binding var isCalibrating: Bool
    @Binding var complete: Bool

    var body: some View {
        VStack(spacing: 20) {
            Text("步骤 4: 生成 GSDF LUT")
                .font(.headline)

            if isCalibrating {
                ProgressView()
                Text("正在生成...")
            } else if complete {
                Image(systemName: "checkmark.circle.fill")
                    .font(.system(size: 60))
                    .foregroundColor(.green)
                Text("校准完成!")
            } else {
                Button("生成 GSDF LUT") {
                    generateGSDF()
                }
            }
        }
    }

    private func generateGSDF() {
        isCalibrating = true
        // 调用 SDK display_generate_gsdf_lut
        DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
            isCalibrating = false
            complete = true
        }
    }
}
```

- [ ] **Step 2: 提交**

```bash
git add examples/macos/MedicalDisplay/Views/CalibrationWizard.swift
git commit -m "feat(calibration): add GSDF calibration wizard"
```

---

## Phase 4: 调试与优化 (D)

### Task 10: 添加 SDK 调试日志

**Files:**
- Modify: `sdk/display_engine/include/display_engine.h` (添加调试宏)

- [ ] **Step 1: 添加调试宏**

```c
// display_engine.h 添加

#ifdef DEBUG
#define MEDDISP_DEBUG(fmt, ...) \
    fprintf(stderr, "[MEDDISP DEBUG %s:%d] " fmt "\n", \
            __FILE__, __LINE__, ##__VA_ARGS__)
#define MEDDISP_DEBUG_LOG_ENABLED 1
#else
#define MEDDISP_DEBUG(fmt, ...) ((void)0)
#define MEDDISP_DEBUG_LOG_ENABLED 0
#endif
```

- [ ] **Step 2: 在关键函数添加日志**

```c
// display_engine_impl.cpp
void display_engine_render_frame(DisplayEngine* engine, ...) {
    MEDDISP_DEBUG("render_frame start: width=%d, height=%d", width, height);
    // ... 实现
    MEDDISP_DEBUG("render_frame end");
}
```

- [ ] **Step 3: 提交**

```bash
git add sdk/display_engine/include/display_engine.h sdk/display_engine/src/display_engine_impl.cpp
git commit -m "debug: add SDK debug logging macros"
```

---

### Task 11: 性能基准测试

**Files:**
- Create: `tests/benchmark/metal_benchmark.cpp`

- [ ] **Step 1: 创建 Metal 基准测试**

```cpp
// tests/benchmark/metal_benchmark.cpp
#include <iostream>
#include <chrono>
#include <Metal/Metal.hpp>

void benchmark_process_frame(int width, int height, int iterations) {
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        // 调用 Metal 渲染
        process_frame_test(width, height);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double fps = iterations * 1000.0 / duration.count();
    double ms_per_frame = duration.count() / (double)iterations;

    printf("Resolution: %dx%d\n", width, height);
    printf("Frames: %d\n", iterations);
    printf("Total time: %.2f ms\n", duration.count());
    printf("Per frame: %.2f ms\n", ms_per_frame);
    printf("FPS: %.1f\n", fps);
}
```

- [ ] **Step 2: 提交**

```bash
git add tests/benchmark/metal_benchmark.cpp
git commit -m "test: add Metal performance benchmark"
```

---

## 任务依赖关系

```
Task 1 (项目基础)
    └── Task 2 (ContentView)
            ├── Task 3 (RenderView)
            ├── Task 4 (ControlPanel)
            └── Task 5 (StatusBar)
                    │
                    ├── Task 6 (下载模型)
                    │       └── Task 7 (ONNX 集成)
                    │               └── Task 8 (AIPanel)
                    │
                    └── Task 9 (CalibrationWizard)
                            │
                            └── Task 10-11 (调试优化)
```

---

## 验收标准检查清单

### A. macOS 应用框架
- [ ] 应用可正常启动
- [ ] 可加载图像文件
- [ ] Metal 渲染正常工作
- [ ] 参数滑块可调节

### B. AI 推理
- [ ] 模型推理正常执行
- [ ] 模态识别结果合理
- [ ] 无模型时规则引擎回退正常

### C. GSDF 校准
- [ ] 校准向导可完整执行
- [ ] 生成的 GSDF LUT 正确
- [ ] 配置文件可保存/加载

### D. 调试优化
- [ ] 调试日志正常工作
- [ ] 基准测试可运行
- [ ] 无明显内存泄漏
