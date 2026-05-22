import AppKit
import SwiftUI
import UniformTypeIdentifiers

class AppDelegate: NSObject, NSApplicationDelegate {
    var window: NSWindow!
    var sharedAppState = AppState()

    func applicationDidFinishLaunching(_ notification: Notification) {
        setupMenuBar()

        let contentView = ContentView()

        window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 1400, height: 900),
            styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView],
            backing: .buffered,
            defer: false
        )
        window.center()
        window.setFrameAutosaveName("MedicalDisplayMainWindow")
        window.contentView = NSHostingView(rootView: contentView)
        window.title = "AI Medical Display"
        window.titlebarAppearsTransparent = false
        window.makeKeyAndOrderFront(nil)

        // Setup toolbar
        setupToolbar()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return true
    }

    // MARK: - Menu Bar

    private func setupMenuBar() {
        let mainMenu = NSMenu()

        // App Menu
        let appMenuItem = NSMenuItem()
        mainMenu.addItem(appMenuItem)
        let appMenu = NSMenu()
        appMenuItem.submenu = appMenu

        appMenu.addItem(NSMenuItem(title: "关于 AI Medical Display", action: #selector(showAbout), keyEquivalent: ""))
        appMenu.addItem(NSMenuItem.separator())
        appMenu.addItem(NSMenuItem(title: "偏好设置...", action: #selector(showPreferences), keyEquivalent: ","))
        appMenu.addItem(NSMenuItem.separator())
        appMenu.addItem(NSMenuItem(title: "退出", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q"))

        // File Menu
        let fileMenuItem = NSMenuItem()
        mainMenu.addItem(fileMenuItem)
        let fileMenu = NSMenu(title: "文件")
        fileMenuItem.submenu = fileMenu

        fileMenu.addItem(NSMenuItem(title: "打开图像...", action: #selector(openImage), keyEquivalent: "o"))
        fileMenu.addItem(NSMenuItem(title: "打开 DICOM...", action: #selector(openDicom), keyEquivalent: "O"))
        fileMenu.addItem(NSMenuItem.separator())
        fileMenu.addItem(NSMenuItem(title: "关闭", action: #selector(closeDocument), keyEquivalent: "w"))

        // View Menu
        let viewMenuItem = NSMenuItem()
        mainMenu.addItem(viewMenuItem)
        let viewMenu = NSMenu(title: "视图")
        viewMenuItem.submenu = viewMenu

        viewMenu.addItem(NSMenuItem(title: "显示/隐藏控制面板", action: #selector(toggleControlPanel), keyEquivalent: "1"))
        viewMenu.addItem(NSMenuItem(title: "显示/隐藏 AI 面板", action: #selector(toggleAIPanel), keyEquivalent: "2"))
        viewMenu.addItem(NSMenuItem.separator())
        viewMenu.addItem(NSMenuItem(title: "进入全屏", action: #selector(toggleFullScreen), keyEquivalent: "f"))

        // Edit Menu
        let editMenuItem = NSMenuItem()
        mainMenu.addItem(editMenuItem)
        let editMenu = NSMenu(title: "编辑")
        editMenuItem.submenu = editMenu

        editMenu.addItem(NSMenuItem(title: "撤销", action: #selector(undo), keyEquivalent: "z"))
        editMenu.addItem(NSMenuItem(title: "重做", action: #selector(redo), keyEquivalent: "Z"))
        editMenu.addItem(NSMenuItem.separator())
        editMenu.addItem(NSMenuItem(title: "重置参数", action: #selector(resetToDefaults), keyEquivalent: "r"))

        // Calibration Menu
        let calibMenuItem = NSMenuItem()
        mainMenu.addItem(calibMenuItem)
        let calibMenu = NSMenu(title: "校准")
        calibMenuItem.submenu = calibMenu

        calibMenu.addItem(NSMenuItem(title: "GSDF 校准向导...", action: #selector(showCalibrationWizard), keyEquivalent: "k"))
        calibMenu.addItem(NSMenuItem(title: "加载校准配置...", action: #selector(loadCalibrationProfile), keyEquivalent: ""))
        calibMenu.addItem(NSMenuItem(title: "保存校准配置...", action: #selector(saveCalibrationProfile), keyEquivalent: ""))
        calibMenu.addItem(NSMenuItem.separator())
        calibMenu.addItem(NSMenuItem(title: "重置为默认", action: #selector(resetToDefaults), keyEquivalent: ""))

        // Window Menu
        let windowMenuItem = NSMenuItem()
        mainMenu.addItem(windowMenuItem)
        let windowMenu = NSMenu(title: "窗口")
        windowMenuItem.submenu = windowMenu

        windowMenu.addItem(NSMenuItem(title: "最小化", action: #selector(NSWindow.miniaturize(_:)), keyEquivalent: "m"))
        windowMenu.addItem(NSMenuItem(title: "缩放", action: #selector(NSWindow.zoom(_:)), keyEquivalent: ""))

        // Help Menu
        let helpMenuItem = NSMenuItem()
        mainMenu.addItem(helpMenuItem)
        let helpMenu = NSMenu(title: "帮助")
        helpMenuItem.submenu = helpMenu

        helpMenu.addItem(NSMenuItem(title: "AI Medical Display 帮助", action: #selector(showHelp), keyEquivalent: "?"))

        NSApplication.shared.mainMenu = mainMenu
    }

    // MARK: - Toolbar

    private func setupToolbar() {
        let toolbar = NSToolbar(identifier: "MedicalDisplayToolbar")
        toolbar.delegate = self
        toolbar.displayMode = .iconAndLabel
        toolbar.allowsUserCustomization = true
        toolbar.autosavesConfiguration = true

        window?.toolbar = toolbar
    }

    // MARK: - Actions

    @objc func openImage() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.image, .png, .jpeg, .tiff]
        panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url {
            sharedAppState.loadImage(from: url)
        }
    }

    @objc func openDicom() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.data]
        panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url {
            sharedAppState.loadDicom(from: url)
        }
    }

    @objc func showCalibrationWizard() {
        let wizardView = CalibrationWizard()
        let hostingController = NSHostingController(rootView: wizardView)

        let wizardWindow = NSWindow(contentViewController: hostingController)
        wizardWindow.title = "GSDF 校准向导"
        wizardWindow.setContentSize(NSSize(width: 500, height: 400))
        wizardWindow.styleMask = [.titled, .closable]
        wizardWindow.center()
        wizardWindow.makeKeyAndOrderFront(nil)
    }

    @objc func loadCalibrationProfile() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.json]
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false

        if panel.runModal() == .OK, let url = panel.url {
            do {
                let data = try Data(contentsOf: url)
                let decoder = JSONDecoder()
                decoder.dateDecodingStrategy = .iso8601
                let profile = try decoder.decode(CalibrationProfile.self, from: data)
                applyCalibrationProfile(profile)
                showAlert(title: "加载成功", message: "校准配置已加载: \(profile.displayId)")
            } catch {
                showAlert(title: "加载失败", message: "无法加载校准配置: \(error.localizedDescription)")
            }
        }
    }

    @objc func saveCalibrationProfile() {
        let profile = createCalibrationProfileFromCurrentState()
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.json]
        panel.nameFieldStringValue = "\(profile.displayId)_\(formatDate(profile.calibrationDate)).json"

        if panel.runModal() == .OK, let url = panel.url {
            do {
                let encoder = JSONEncoder()
                encoder.dateEncodingStrategy = .iso8601
                encoder.outputFormatting = .prettyPrinted
                let data = try encoder.encode(profile)
                try data.write(to: url)
                showAlert(title: "保存成功", message: "校准配置已保存")
            } catch {
                showAlert(title: "保存失败", message: "无法保存校准配置: \(error.localizedDescription)")
            }
        }
    }

    private func createCalibrationProfileFromCurrentState() -> CalibrationProfile {
        return CalibrationProfile(
            displayId: "built-in-display",
            calibrationDate: Date(),
            luminance: CalibrationProfile.LuminanceConfig(black: 0.5, white: 450.0),
            ambientLight: 50.0,
            gsdfLutPath: nil,
            deltaE: 1.8
        )
    }

    private func applyCalibrationProfile(_ profile: CalibrationProfile) {
        // 应用校准配置到 AppState
        sharedAppState.enableGsdf = true
    }

    private func formatDate(_ date: Date) -> String {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd_HHmmss"
        return formatter.string(from: date)
    }

    private func showAlert(title: String, message: String) {
        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = message
        alert.alertStyle = .informational
        alert.addButton(withTitle: "确定")
        alert.runModal()
    }

    @objc func undo() {
        sharedAppState.undo()
    }

    @objc func redo() {
        sharedAppState.redo()
    }

    @objc func resetToDefaults() {
        sharedAppState.resetToDefaults()
    }

    @objc func showAbout() {
        NSApplication.shared.orderFrontStandardAboutPanel(nil)
    }

    @objc func showPreferences() {
        // TODO: 实现偏好设置
    }

    @objc func showHelp() {
        // TODO: 实现帮助
    }

    @objc func closeDocument() {
        sharedAppState.currentImagePath = nil
    }

    @objc func toggleControlPanel() {
        // TODO: 控制面板显示/隐藏
    }

    @objc func toggleAIPanel() {
        // TODO: AI 面板显示/隐藏
    }

    @objc func toggleFullScreen() {
        window?.toggleFullScreen(nil)
    }
}

// MARK: - Toolbar Delegate

extension AppDelegate: NSToolbarDelegate {
    func toolbar(_ toolbar: NSToolbar, itemForItemIdentifier itemIdentifier: NSToolbarItem.Identifier, willBeInsertedIntoToolbar flag: Bool) -> NSToolbarItem? {
        switch itemIdentifier.rawValue {
        case "openImage":
            let item = NSToolbarItem(itemIdentifier: itemIdentifier)
            item.label = "打开图像"
            item.paletteLabel = "打开图像"
            item.toolTip = "打开图像文件"
            item.image = NSImage(systemSymbolName: "photo", accessibilityDescription: "打开图像")
            item.target = self
            item.action = #selector(openImage)
            return item

        case "openDicom":
            let item = NSToolbarItem(itemIdentifier: itemIdentifier)
            item.label = "打开 DICOM"
            item.paletteLabel = "打开 DICOM"
            item.toolTip = "打开 DICOM 文件"
            item.image = NSImage(systemSymbolName: "doc.text.image", accessibilityDescription: "打开 DICOM")
            item.target = self
            item.action = #selector(openDicom)
            return item

        case "calibration":
            let item = NSToolbarItem(itemIdentifier: itemIdentifier)
            item.label = "校准"
            item.paletteLabel = "校准向导"
            item.toolTip = "打开校准向导"
            item.image = NSImage(systemSymbolName: "slider.horizontal.3", accessibilityDescription: "校准")
            item.target = self
            item.action = #selector(showCalibrationWizard)
            return item

        case "toggleGsdf":
            let item = NSToolbarItem(itemIdentifier: itemIdentifier)
            item.label = "GSDF"
            item.paletteLabel = "启用 GSDF"
            item.toolTip = "启用/禁用 GSDF 校准"
            item.image = NSImage(systemSymbolName: "waveform", accessibilityDescription: "GSDF")
            item.target = self
            item.action = #selector(toggleGsdf)
            return item

        default:
            return nil
        }
    }

    func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        return [
            NSToolbarItem.Identifier("openImage"),
            NSToolbarItem.Identifier("openDicom"),
            .flexibleSpace,
            NSToolbarItem.Identifier("calibration"),
            NSToolbarItem.Identifier("toggleGsdf")
        ]
    }

    func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        return toolbarDefaultItemIdentifiers(toolbar)
    }

    @objc func toggleGsdf() {
        sharedAppState.enableGsdf.toggle()
    }
}