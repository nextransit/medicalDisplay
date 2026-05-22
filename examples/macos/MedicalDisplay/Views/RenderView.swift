import SwiftUI
import MetalKit

struct RenderView: NSViewRepresentable {
    @ObservedObject var appState: AppState

    func makeNSView(context: Context) -> RenderingView {
        let view = RenderingView()
        view.device = MTLCreateSystemDefaultDevice()
        view.renderer = MetalRenderer()
        view.appState = appState
        return view
    }

    func updateNSView(_ nsView: RenderingView, context: Context) {
        nsView.appState = appState
    }
}

class RenderingView: MTKView {
    var renderer: MetalRenderer?
    var appState: AppState?
    var currentScale: CGFloat = 1.0
    var currentOffset: CGSize = .zero

    override init(frame frameRect: CGRect, device: MTLDevice?) {
        super.init(frame: frameRect, device: device)
        setupGestures()
    }

    required init(coder: NSCoder) {
        super.init(coder: coder)
        setupGestures()
    }

    private func setupGestures() {
        let magnificationGesture = NSMagnificationGestureRecognizer(target: self, action: #selector(handleMagnification(_:)))
        self.addGestureRecognizer(magnificationGesture)

        let panGesture = NSPanGestureRecognizer(target: self, action: #selector(handlePan(_:)))
        self.addGestureRecognizer(panGesture)

        let clickGesture = NSClickGestureRecognizer(target: self, action: #selector(handleDoubleClick(_:)))
        self.addGestureRecognizer(clickGesture)

        self.delegate = self
        self.enableSetNeedsDisplay = true
        self.isPaused = false
    }

    @objc private func handleMagnification(_ gesture: NSMagnificationGestureRecognizer) {
        currentScale = max(0.1, min(10.0, currentScale * (1.0 + gesture.magnification)))
        gesture.magnification = 0
        needsDisplay = true
    }

    @objc private func handlePan(_ gesture: NSPanGestureRecognizer) {
        let translation = gesture.translation(in: self)
        currentOffset = CGSize(
            width: currentOffset.width + translation.x,
            height: currentOffset.height + translation.y
        )
        gesture.setTranslation(.zero, in: self)
        needsDisplay = true
    }

    @objc private func handleDoubleClick(_ gesture: NSClickGestureRecognizer) {
        currentScale = 1.0
        currentOffset = .zero
        needsDisplay = true
    }
}

extension RenderingView: MTKViewDelegate {
    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    func draw(in view: MTKView) {
        guard let view = view as? RenderingView,
              let renderer = view.renderer,
              let appState = view.appState else { return }

        // Apply transform
        if view.currentScale != 1.0 || view.currentOffset != .zero {
            view.layer?.setAffineTransform(
                CGAffineTransform(scaleX: view.currentScale, y: view.currentScale)
                    .translatedBy(x: view.currentOffset.width / view.currentScale, y: view.currentOffset.height / view.currentScale)
            )
        } else {
            view.layer?.setAffineTransform(.identity)
        }

        renderer.render(appState: appState, in: view)
    }
}