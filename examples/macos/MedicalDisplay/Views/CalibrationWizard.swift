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
