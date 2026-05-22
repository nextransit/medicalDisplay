import Foundation
import Combine
import AppKit

class AppState: ObservableObject {
    @Published var currentModality: String = "Unknown"
    @Published var confidence: Float = 0.0
    @Published var brightness: Float = 0.0
    @Published var contrast: Float = 1.0
    @Published var saturation: Float = 1.0
    @Published var enableGsdf: Bool = true
    @Published var enableBloodless: Bool = false
    @Published var enableSobel: Bool = false
    @Published var sobelThreshold: Float = 0.3
    @Published var bloodSuppress: Float = 0.5
    @Published var tissueEnhance: Float = 0.3
    @Published var isProcessing: Bool = false
    @Published var currentImagePath: String?

    // DICOM specific
    @Published var windowCenter: Float = 40
    @Published var windowWidth: Float = 400
    @Published var dicomMetadata: DicomBridge.Metadata?
    @Published var currentFrameIndex: Int = 0
    @Published var totalFrames: Int = 1
    @Published var isMultiFrameDicom: Bool = false

    let aiEngine = AIEngineBridge()
    let dicomBridge = DicomBridge()
    private var cancellables = Set<AnyCancellable>()

    init() {
        // 监听图像路径变化，自动触发 AI 识别
        $currentImagePath
            .dropFirst()
            .sink { [weak self] path in
                self?.processImage(at: path)
            }
            .store(in: &cancellables)
    }

    func loadImage(from url: URL) {
        currentImagePath = url.path
        dicomMetadata = nil
    }

    func loadDicom(from url: URL) {
        isProcessing = true

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self = self else { return }

            // 使用 DicomBridge 读取元数据
            if let metadata = self.dicomBridge.readMetadata(from: url) {
                DispatchQueue.main.async {
                    self.dicomMetadata = metadata
                    self.currentModality = metadata.modality
                    self.windowCenter = metadata.windowCenter
                    self.windowWidth = metadata.windowWidth
                    self.currentImagePath = url.path
                    self.applyRecommendedParams(for: metadata.modality)
                }
            } else {
                // 回退到普通图像处理
                DispatchQueue.main.async {
                    self.loadImage(from: url)
                }
            }

            DispatchQueue.main.async {
                self.isProcessing = false
            }
        }
    }

    private func processImage(at path: String?) {
        guard let path = path else { return }

        isProcessing = true

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self = self else { return }

            // 加载图像数据
            guard let imageData = FileManager.default.contents(atPath: path),
                  let cgImage = NSImage(byReferencingFile: path)?.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
                DispatchQueue.main.async {
                    self.isProcessing = false
                }
                return
            }

            let width = cgImage.width
            let height = cgImage.height

            // 提取 RGB 数据
            var rgbData = [UInt8](repeating: 0, count: width * height * 3)
            let colorSpace = CGColorSpaceCreateDeviceRGB()
            guard let context = CGContext(data: &rgbData,
                                         width: width,
                                         height: height,
                                         bitsPerComponent: 8,
                                         bytesPerRow: width * 3,
                                         space: colorSpace,
                                         bitmapInfo: CGImageAlphaInfo.none.rawValue) else {
                DispatchQueue.main.async {
                    self.isProcessing = false
                }
                return
            }
            context.draw(cgImage, in: CGRect(x: 0, y: 0, width: width, height: height))

            // AI 识别
            let (modality, conf) = self.aiEngine.recognize(imageData: rgbData, width: width, height: height)

            DispatchQueue.main.async {
                self.currentModality = modality
                self.confidence = conf
                self.isProcessing = false
                self.applyRecommendedParams(for: modality)
            }
        }
    }

    private func applyRecommendedParams(for modality: String) {
        switch modality {
        case "CT":
            brightness = 0.05
            contrast = 1.15
            windowCenter = 40
            windowWidth = 400
        case "MRI":
            brightness = 0.1
            contrast = 1.2
            windowCenter = 127
            windowWidth = 256
        case "XRay":
            brightness = 0.0
            contrast = 1.1
            windowCenter = 2000
            windowWidth = 4000
        case "Ultrasound":
            brightness = 0.15
            contrast = 1.0
            windowCenter = 50
            windowWidth = 200
        case "PET":
            brightness = 0.2
            contrast = 1.3
            windowCenter = 150
            windowWidth = 500
        default:
            break
        }
    }

    // MARK: - Frame Navigation

    func nextFrame() {
        if currentFrameIndex < totalFrames - 1 {
            currentFrameIndex += 1
            loadFrame(currentFrameIndex)
        }
    }

    func previousFrame() {
        if currentFrameIndex > 0 {
            currentFrameIndex -= 1
            loadFrame(currentFrameIndex)
        }
    }

    func goToFrame(_ index: Int) {
        guard index >= 0 && index < totalFrames else { return }
        currentFrameIndex = index
        loadFrame(currentFrameIndex)
    }

    private func loadFrame(_ index: Int) {
        // TODO: 加载指定帧
        // 需要集成 SDK 的 dicom_read_frame
    }

    // MARK: - Undo/Redo Support

    struct ParamSnapshot: Codable {
        var brightness: Float
        var contrast: Float
        var saturation: Float
        var enableGsdf: Bool
        var enableBloodless: Bool
        var enableSobel: Bool
        var sobelThreshold: Float
        var bloodSuppress: Float
        var tissueEnhance: Float
        var windowCenter: Float
        var windowWidth: Float
    }

    private var undoStack: [ParamSnapshot] = []
    private var redoStack: [ParamSnapshot] = []
    private let maxUndoLevels = 20

    var canUndo: Bool { !undoStack.isEmpty }
    var canRedo: Bool { !redoStack.isEmpty }

    func saveSnapshot() {
        let snapshot = ParamSnapshot(
            brightness: brightness,
            contrast: contrast,
            saturation: saturation,
            enableGsdf: enableGsdf,
            enableBloodless: enableBloodless,
            enableSobel: enableSobel,
            sobelThreshold: sobelThreshold,
            bloodSuppress: bloodSuppress,
            tissueEnhance: tissueEnhance,
            windowCenter: windowCenter,
            windowWidth: windowWidth
        )
        undoStack.append(snapshot)
        if undoStack.count > maxUndoLevels {
            undoStack.removeFirst()
        }
        redoStack.removeAll()
    }

    func undo() {
        guard let snapshot = undoStack.popLast() else { return }

        // 保存当前状态到 redo
        let current = ParamSnapshot(
            brightness: brightness,
            contrast: contrast,
            saturation: saturation,
            enableGsdf: enableGsdf,
            enableBloodless: enableBloodless,
            enableSobel: enableSobel,
            sobelThreshold: sobelThreshold,
            bloodSuppress: bloodSuppress,
            tissueEnhance: tissueEnhance,
            windowCenter: windowCenter,
            windowWidth: windowWidth
        )
        redoStack.append(current)

        // 恢复快照
        restoreSnapshot(snapshot)
    }

    func redo() {
        guard let snapshot = redoStack.popLast() else { return }

        // 保存当前状态到 undo
        let current = ParamSnapshot(
            brightness: brightness,
            contrast: contrast,
            saturation: saturation,
            enableGsdf: enableGsdf,
            enableBloodless: enableBloodless,
            enableSobel: enableSobel,
            sobelThreshold: sobelThreshold,
            bloodSuppress: bloodSuppress,
            tissueEnhance: tissueEnhance,
            windowCenter: windowCenter,
            windowWidth: windowWidth
        )
        undoStack.append(current)

        // 恢复快照
        restoreSnapshot(snapshot)
    }

    func resetToDefaults() {
        saveSnapshot()
        brightness = 0.0
        contrast = 1.0
        saturation = 1.0
        enableGsdf = true
        enableBloodless = false
        enableSobel = false
        sobelThreshold = 0.3
        bloodSuppress = 0.5
        tissueEnhance = 0.3
        windowCenter = 40
        windowWidth = 400
    }

    private func restoreSnapshot(_ snapshot: ParamSnapshot) {
        brightness = snapshot.brightness
        contrast = snapshot.contrast
        saturation = snapshot.saturation
        enableGsdf = snapshot.enableGsdf
        enableBloodless = snapshot.enableBloodless
        enableSobel = snapshot.enableSobel
        sobelThreshold = snapshot.sobelThreshold
        bloodSuppress = snapshot.bloodSuppress
        tissueEnhance = snapshot.tissueEnhance
        windowCenter = snapshot.windowCenter
        windowWidth = snapshot.windowWidth
    }
}
