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
