import SwiftUI

struct DebugConsoleView: View {
    @ObservedObject var appState: AppState
    @State private var isExpanded = false

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            // Header
            HStack {
                Text("调试信息")
                    .font(.headline)

                Spacer()

                Button(action: { isExpanded.toggle() }) {
                    Image(systemName: isExpanded ? "chevron.down" : "chevron.up")
                }
                .buttonStyle(.plain)

                Button(action: { copyDebugInfo() }) {
                    Image(systemName: "doc.on.doc")
                }
                .buttonStyle(.plain)
            }

            if isExpanded {
                Divider()

                VStack(alignment: .leading, spacing: 2) {
                    Group {
                        debugSection("渲染", [
                            "FPS: \(String(format: "%.1f", 60.0))",
                            "分辨率: \(Int(MTKView().drawableSize.width))x\(Int(MTKView().drawableSize.height))",
                            "格式: RGBA32Float",
                        ])

                        debugSection("显示参数", [
                            "亮度: \(String(format: "%.2f", appState.brightness))",
                            "对比度: \(String(format: "%.2f", appState.contrast))",
                            "饱和度: \(String(format: "%.2f", appState.saturation))",
                        ])

                        debugSection("GSDF", [
                            "启用: \(appState.enableGsdf ? "是" : "否")",
                            "窗口中心: \(String(format: "%.0f", appState.windowCenter))",
                            "窗口宽度: \(String(format: "%.0f", appState.windowWidth))",
                        ])

                        debugSection("AI", [
                            "模态: \(appState.currentModality)",
                            "置信度: \(String(format: "%.1f%%", appState.confidence * 100))",
                        ])

                        debugSection("功能", [
                            "Sobel: \(appState.enableSobel ? "启用" : "禁用")",
                            "无血术野: \(appState.enableBloodless ? "启用" : "禁用")",
                        ])
                    }
                }
                .font(.system(size: 10, design: .monospaced))
            }
        }
        .padding(8)
        .background(Color(NSColor.windowBackgroundColor))
        .cornerRadius(8)
        .frame(maxWidth: 300)
    }

    private func debugSection(_ title: String, _ items: [String]) -> some View {
        VStack(alignment: .leading, spacing: 1) {
            Text(title)
                .font(.caption)
                .foregroundColor(.secondary)
            ForEach(items, id: \.self) { item in
                Text(item)
            }
        }
    }

    private func copyDebugInfo() {
        let info = """
        === AI Medical Display 调试信息 ===
        时间: \(Date())

        渲染:
        - FPS: 60.0
        - 分辨率: 未知

        显示参数:
        - 亮度: \(appState.brightness)
        - 对比度: \(appState.contrast)
        - 饱和度: \(appState.saturation)

        GSDF:
        - 启用: \(appState.enableGsdf)
        - 窗口中心: \(appState.windowCenter)
        - 窗口宽度: \(appState.windowWidth)

        AI:
        - 模态: \(appState.currentModality)
        - 置信度: \(appState.confidence)

        功能:
        - Sobel: \(appState.enableSobel)
        - 无血术野: \(appState.enableBloodless)
        """

        #if os(macOS)
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(info, forType: .string)
        #endif
    }
}
