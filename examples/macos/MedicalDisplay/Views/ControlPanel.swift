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
