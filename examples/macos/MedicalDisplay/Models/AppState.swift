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
    @Published var isProcessing: Bool = false
    @Published var currentImagePath: String?

    let aiEngine = AIEngineBridge()
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
    }

    func loadDicom(from url: URL) {
        // DICOM 解析 - 使用 SDK
        // TODO: 集成 SDK dicom_reader
        // 目前先作为普通图像处理
        loadImage(from: url)
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
        case "MRI":
            brightness = 0.1
            contrast = 1.2
        case "XRay":
            brightness = 0.0
            contrast = 1.1
        case "Ultrasound":
            brightness = 0.15
            contrast = 1.0
        case "PET":
            brightness = 0.2
            contrast = 1.3
        default:
            break
        }
    }
}
