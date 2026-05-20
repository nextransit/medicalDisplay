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
