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
                DicomNavigatorView(appState: appState)
                StatusBar(appState: appState)
            }

            // 右侧 AI 面板
            AIPanel(appState: appState)
                .frame(minWidth: 250, maxWidth: 300)
        }
    }
}
