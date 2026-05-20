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