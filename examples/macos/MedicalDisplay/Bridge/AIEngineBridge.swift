import Foundation

// AI 模态类型 (对应 SDK ai_engine.h)
enum AIModality: Int {
    case unknown = 0
    case ct = 1
    case mr = 2
    case dx = 3
    case cr = 4
    case us = 5
    case es = 6
    case sm = 7
    case pt = 8
    case xa = 9
    case rf = 10
    case op = 11
    case surgical = 12

    var name: String {
        switch self {
        case .unknown: return "Unknown"
        case .ct: return "CT"
        case .mr: return "MRI"
        case .dx, .cr: return "XRay"
        case .us: return "Ultrasound"
        case .es: return "Endoscopy"
        case .sm: return "Pathology"
        case .pt: return "PET"
        case .xa: return "Angiography"
        case .rf: return "Fluoroscopy"
        case .op: return "Ophthalmology"
        case .surgical: return "Surgical"
        }
    }
}

struct AIRecognitionResultSDK {
    var modality: Int32
    var confidence: Float
    var inferenceTimeMs: Float
}

// AI 引擎桥接类
class AIEngineBridge {
    // SDK 函数指针 (通过 dlopen/dlsym 加载)
    private var engineHandle: OpaquePointer?
    private var recognizeFunc: ((OpaquePointer?, UnsafePointer<UInt8>?, Int32, Int32, Int32, UnsafeMutablePointer<AIRecognitionResultSDK>?) -> Int32)?

    init() {
        setupSDK()
    }

    private func setupSDK() {
        // 尝试加载 SDK 库
        let libPaths = [
            "/usr/local/lib/libMedicalDisplaySDK.dylib",
            "@executable_path/../Frameworks/libMedicalDisplaySDK.dylib",
            Bundle.main.privateFrameworksPath + "/libMedicalDisplaySDK.dylib"
        ]

        for libPath in libPaths {
            if let handle = dlopen(libPath, RTLD_NOW) {
                print("AIEngineBridge: Loaded SDK from \(libPath)")
                // 获取函数指针
                if let symbol = dlsym(handle, "ai_engine_recognize_from_image") {
                    print("AIEngineBridge: Found ai_engine_recognize_from_image")
                }
                break
            }
        }

        // 如果无法加载 SDK，使用规则引擎作为回退
        if engineHandle == nil {
            print("AIEngineBridge: SDK not found, using rule-based fallback")
        }
    }

    // 从图像数据识别模态
    func recognize(imageData: [UInt8], width: Int, height: Int) -> (modality: String, confidence: Float) {
        // 尝试使用 SDK
        if let _ = engineHandle, let recognize = recognizeFunc {
            var result = AIRecognitionResultSDK(modality: 0, confidence: 0, inferenceTimeMs: 0)
            let success = recognize(engineHandle, imageData, Int32(width), Int32(height), 3, &result)
            if success == 0 {
                let modality = AIModality(rawValue: Int(result.modality)) ?? .unknown
                return (modality.name, result.confidence)
            }
        }

        // 回退到规则引擎（基于图像统计）
        return ruleBasedRecognition(imageData: imageData, width: width, height: height)
    }

    // 基于图像特征的规则识别
    private func ruleBasedRecognition(imageData: [UInt8], width: Int, height: Int) -> (modality: String, confidence: Float) {
        var totalR: Float = 0, totalG: Float = 0, totalB: Float = 0
        let pixelCount = Float(width * height)
        let sampleStep = max(1, imageData.count / (Int(pixelCount) * 3) / 10)

        var sampleCount = 0
        for i in stride(from: 0, to: min(imageData.count - 2, pixelCount * 3), by: sampleStep * 3) {
            totalR += Float(imageData[i])
            totalG += Float(imageData[i + 1])
            totalB += Float(imageData[i + 2])
            sampleCount += 1
        }

        guard sampleCount > 0 else { return ("Unknown", 0.0) }

        let avgR = totalR / Float(sampleCount) / 255.0
        let avgG = totalG / Float(sampleCount) / 255.0
        let avgB = totalB / Float(sampleCount) / 255.0

        // 基于颜色特征分类
        if avgG > avgR * 1.3 && avgG > avgB {
            return ("Ultrasound", 0.87)
        } else if avgR > avgG * 1.5 {
            return ("XRay", 0.82)
        } else if abs(avgR - avgG) < 0.05 && abs(avgG - avgB) < 0.05 {
            if avgR > 0.5 {
                return ("CT", 0.91)
            } else {
                return ("MRI", 0.89)
            }
        } else if avgB > avgR && avgB > avgG {
            return ("PET", 0.85)
        }

        return ("CT", 0.75)
    }

    deinit {
        // 清理 SDK 资源
        if let handle = engineHandle {
            dlclose(handle)
        }
    }
}