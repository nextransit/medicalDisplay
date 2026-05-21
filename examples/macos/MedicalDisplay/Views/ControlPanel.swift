import SwiftUI

struct WindowPreset: Identifiable {
    let id = UUID()
    let name: String
    let center: Float
    let width: Float
}

struct ControlPanel: View {
    @ObservedObject var appState: AppState

    let windowPresets: [WindowPreset] = [
        WindowPreset(name: "CT 骨", center: 300, width: 1500),
        WindowPreset(name: "CT 肺", center: -600, width: 1600),
        WindowPreset(name: "CT 软组织", center: 40, width: 400),
        WindowPreset(name: "CT 脑", center: 40, width: 80),
        WindowPreset(name: "MRI T1", center: 500, width: 1000),
        WindowPreset(name: "MRI T2", center: 100, width: 2000),
    ]

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

            // 窗口/级别预设
            GroupBox("窗口预设") {
                VStack(alignment: .leading, spacing: 8) {
                    Text("窗口中心: \(String(format: "%.0f", appState.windowCenter))")
                    Text("窗口宽度: \(String(format: "%.0f", appState.windowWidth))")

                    Divider()

                    ForEach(windowPresets) { preset in
                        Button(preset.name) {
                            applyWindowPreset(preset)
                        }
                        .buttonStyle(.bordered)
                    }
                }
            }

            // 功能开关
            GroupBox("功能") {
                Toggle("启用 GSDF", isOn: $appState.enableGsdf)
                Toggle("无血术野增强", isOn: $appState.enableBloodless)
                Toggle("Sobel 边缘检测", isOn: $appState.enableSobel)
            }

            // Sobel 边缘检测参数
            GroupBox("边缘检测参数") {
                VStack(alignment: .leading) {
                    Text("阈值: \(String(format: "%.2f", appState.sobelThreshold))")
                    Slider(value: $appState.sobelThreshold, in: 0.1...0.9)
                }
            }

            // 无血术野增强参数
            GroupBox("术野增强参数") {
                VStack(alignment: .leading) {
                    Text("血色抑制: \(String(format: "%.0f%%", appState.bloodSuppress * 100))")
                    Slider(value: $appState.bloodSuppress, in: 0...1)

                    Text("组织增强: \(String(format: "%.0f%%", appState.tissueEnhance * 100))")
                    Slider(value: $appState.tissueEnhance, in: 0...1)
                }
            }

            Spacer()
        }
        .padding()
        .frame(minWidth: 280)
    }

    func openImage() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.image, .png, .jpeg, .tiff, .dicom]
        panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url {
            appState.loadImage(from: url)
        }
    }

    func openDicom() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.data]
        panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url {
            appState.loadDicom(from: url)
        }
    }

    func applyWindowPreset(_ preset: WindowPreset) {
        appState.windowCenter = preset.center
        appState.windowWidth = preset.width
    }
}
