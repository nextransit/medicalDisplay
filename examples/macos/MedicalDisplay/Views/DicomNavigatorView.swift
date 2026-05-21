import SwiftUI

struct DicomNavigatorView: View {
    @ObservedObject var appState: AppState

    var body: some View {
        if appState.isMultiFrameDicom {
            VStack(spacing: 8) {
                HStack {
                    Text("帧: \(appState.currentFrameIndex + 1) / \(appState.totalFrames)")
                        .font(.system(.body, design: .monospaced))

                    Spacer()

                    Button(action: { appState.previousFrame() }) {
                        Image(systemName: "chevron.left")
                    }
                    .disabled(appState.currentFrameIndex == 0)

                    Button(action: { appState.nextFrame() }) {
                        Image(systemName: "chevron.right")
                    }
                    .disabled(appState.currentFrameIndex >= appState.totalFrames - 1)
                }

                // 帧滑块
                Slider(
                    value: Binding(
                        get: { Double(appState.currentFrameIndex) },
                        set: { appState.goToFrame(Int($0)) }
                    ),
                    in: 0...Double(max(1, appState.totalFrames - 1)),
                    step: 1
                )

                // 缩略图条（简化版）
                HStack(spacing: 4) {
                    ForEach(0..<min(appState.totalFrames, 10), id: \.self) { idx in
                        let frameIdx = appState.totalFrames > 10 ? idx * appState.totalFrames / 10 : idx
                        Button(action: { appState.goToFrame(frameIdx) }) {
                            RoundedRectangle(cornerRadius: 2)
                                .fill(frameIdx == appState.currentFrameIndex ? Color.blue : Color.gray)
                                .frame(height: 20)
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
            .padding()
            .background(Color(NSColor.windowBackgroundColor))
        }
    }
}
