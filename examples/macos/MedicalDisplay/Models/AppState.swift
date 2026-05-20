import Foundation
import Combine

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
}
