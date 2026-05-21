import Foundation

// GSDF 校准配置文件
struct CalibrationProfile: Codable {
    var displayId: String
    var calibrationDate: Date
    var luminance: LuminanceConfig
    var ambientLight: Float
    var gsdfLutPath: String?
    var deltaE: Float

    struct LuminanceConfig: Codable {
        var black: Float  // cd/m²
        var white: Float  // cd/m²
    }
}

// 校准配置文件管理器
class CalibrationProfileManager {
    static let shared = CalibrationProfileManager()

    private let fileManager = FileManager.default
    private var profilesDirectory: URL {
        let appSupport = fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        return appSupport.appendingPathComponent("MedicalDisplay/Calibration", isDirectory: true)
    }

    init() {
        // 确保目录存在
        try? fileManager.createDirectory(at: profilesDirectory, withIntermediateDirectories: true)
    }

    // 保存校准配置
    func saveProfile(_ profile: CalibrationProfile) throws {
        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .iso8601
        encoder.outputFormatting = .prettyPrinted

        let data = try encoder.encode(profile)
        let fileName = "\(profile.displayId)_\(formatDate(profile.calibrationDate)).json"
        let fileURL = profilesDirectory.appendingPathComponent(fileName)

        try data.write(to: fileURL)
        print("CalibrationProfileManager: Saved profile to \(fileURL.path)")
    }

    // 加载校准配置
    func loadProfile(displayId: String) -> CalibrationProfile? {
        let files: [URL]
        do {
            files = try fileManager.contentsOfDirectory(at: profilesDirectory, includingPropertiesForKeys: nil)
        } catch {
            print("CalibrationProfileManager: Failed to list profiles - \(error)")
            return nil
        }

        // 查找最新的匹配 displayId 的配置
        let matchingFiles = files.filter { $0.lastPathComponent.hasPrefix(displayId) }
            .sorted { url1, url2 in
                let date1 = extractDate(from: url1.lastPathComponent)
                let date2 = extractDate(from: url2.lastPathComponent)
                return date1 > date2
            }

        guard let latestFile = matchingFiles.first else {
            return nil
        }

        do {
            let data = try Data(contentsOf: latestFile)
            let decoder = JSONDecoder()
            decoder.dateDecodingStrategy = .iso8601
            return try decoder.decode(CalibrationProfile.self, from: data)
        } catch {
            print("CalibrationProfileManager: Failed to load profile - \(error)")
            return nil
        }
    }

    // 列出所有配置文件
    func listProfiles() -> [URL] {
        do {
            return try fileManager.contentsOfDirectory(at: profilesDirectory, includingPropertiesForKeys: [.creationDateKey])
                .filter { $0.pathExtension == "json" }
                .sorted { url1, url2 in
                    let date1 = extractDate(from: url1.lastPathComponent)
                    let date2 = extractDate(from: url2.lastPathComponent)
                    return date1 > date2
                }
        } catch {
            print("CalibrationProfileManager: Failed to list profiles - \(error)")
            return []
        }
    }

    // 删除配置文件
    func deleteProfile(at url: URL) throws {
        try fileManager.removeItem(at: url)
    }

    // 获取默认配置
    func getDefaultProfile() -> CalibrationProfile {
        return CalibrationProfile(
            displayId: "default",
            calibrationDate: Date(),
            luminance: CalibrationProfile.LuminanceConfig(black: 0.5, white: 450.0),
            ambientLight: 50.0,
            gsdfLutPath: nil,
            deltaE: 2.0
        )
    }

    // 格式化日期为文件名格式
    private func formatDate(_ date: Date) -> String {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd_HHmmss"
        return formatter.string(from: date)
    }

    // 从文件名提取日期
    private func extractDate(from fileName: String) -> Date {
        let pattern = "_(\\d{4}-\\d{2}-\\d{2})_(\\d{6})"
        guard let regex = try? NSRegularExpression(pattern: pattern),
              let match = regex.firstMatch(in: fileName, range: NSRange(fileName.startIndex..., in: fileName)) else {
            return Date.distantPast
        }

        let dateStr = (fileName as NSString).substring(with: match.range(at: 1))
        let timeStr = (fileName as NSString).substring(with: match.range(at: 2))

        let fullStr = "\(dateStr) \(timeStr.prefix(2)):\(timeStr.prefix(4).suffix(2)):\(timeStr.suffix(2))"
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd HH:mm:ss"
        return formatter.date(from: fullStr) ?? Date.distantPast
    }
}
