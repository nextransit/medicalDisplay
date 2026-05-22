import SwiftUI

struct StatusBar: View {
    @ObservedObject var appState: AppState

    var body: some View {
        HStack {
            // 文件信息
            if let path = appState.currentImagePath {
                Text("文件: \(URL(fileURLWithPath: path).lastPathComponent)")
                    .lineLimit(1)
            } else {
                Text("无图像")
            }

            Spacer()

            // 处理状态
            if appState.isProcessing {
                ProgressView()
                    .scaleEffect(0.7)
                Text("处理中...")
            }

            Divider()
                .frame(height: 16)

            // 帧率
            Text("FPS: \(String(format: "%.1f", currentFps))")
                .foregroundColor(.secondary)

            Divider()
                .frame(height: 16)

            // 模态信息
            Text("模态: \(appState.currentModality)")
                .foregroundColor(.secondary)

            if appState.confidence > 0 {
                Text("(\(Int(appState.confidence * 100))%)")
                    .foregroundColor(.secondary)
            }

            Divider()
                .frame(height: 16)

            // GPU 设备
            Text("GPU: \(gpuDeviceName)")
                .foregroundColor(.secondary)

            // GSDF 状态
            if appState.enableGsdf {
                Text("GSDF")
                    .foregroundColor(.green)
                    .font(.caption)
            }
        }
        .font(.system(size: 11))
        .padding(.horizontal, 8)
        .frame(height: 28)
        .background(Color(NSColor.windowBackgroundColor))
    }

    private var currentFps: Double {
        // 从 RenderView 获取 FPS
        return 60.0 // 占位，需要通过 RenderView 传递
    }

    private var gpuDeviceName: String {
        guard let device = MTLCreateSystemDefaultDevice() else {
            return "N/A"
        }
        return device.name
    }
}
