import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 主窗口 — AI 医学影像显示器
 *
 * 设计理念:
 *   - 深色医学级配色，减少眼疲劳
 *   - 三栏布局：侧边导航 + 影像视口 + AI 分析面板
 *   - 流畅动画过渡，提升专业感
 *   - 完整菜单栏、快捷键系统、状态栏
 */

ApplicationWindow {
    id: mainWindow
    visible: true
    width: 1200
    height: 780
    minimumWidth: 1024
    minimumHeight: 680
    title: "AI Medical Display — 智能医学影像系统"

    // ---- 设计系统 Token ----
    readonly property color colorBg:        "#0a0e17"
    readonly property color colorSurface:   "#141a26"
    readonly property color colorElevated:  "#1a2235"
    readonly property color colorBorder:    "#1e2a3d"
    readonly property color colorAccent:    "#00d4aa"
    readonly property color colorAccentDim: "#009b7d"
    readonly property color colorTextPrimary:   "#e8ecf2"
    readonly property color colorTextSecondary: "#8896a8"
    readonly property color colorTextMuted:     "#556278"
    readonly property color colorSuccess:   "#22c55e"
    readonly property color colorWarning:   "#f59e0b"
    readonly property color colorDanger:    "#ef4444"
    readonly property color colorInfo:      "#3b82f6"

    readonly property int   radiusSm:   6
    readonly property int   radiusMd:   10
    readonly property int   radiusLg:   16
    readonly property int   spacingXs:  4
    readonly property int   spacingSm:  8
    readonly property int   spacingMd:  16
    readonly property int   spacingLg:  24

    // ---- 应用状态 ----
    property int currentModality: 0       // 0:CT 1:MRI 2:XRay 3:超声 4:DICOM
    property real brightness: 0.0
    property real contrast: 1.0
    property real saturation: 1.0
    property bool gsdfEnabled: true
    property bool bloodlessMode: false
    property real bloodSuppress: 0.5
    property real tissueEnhance: 0.3
    property string aiModality: "CT 扫描"
    property real aiConfidence: 0.91
    property string gpuName: "Apple M1 Pro"
    property string gpuVendor: "Apple"
    property real renderFps: 60.0

    // ---- 窗宽/窗位 ----
    property real windowWidthVal: 400
    property real windowCenterVal: 40

    // ---- 窗宽窗位调节 (DICOM 交互) ----
    property real wlCenter: 40
    property real wlWidth: 400
    property real wlCenterOrig: 40
    property real wlWidthOrig: 400
    property real wlDragSensitivity: 0.5

    // ---- 缩放 ----
    property real zoomLevel: 1.0

    // ---- 测量工具状态 ----
    // 0=无(窗宽窗位调节), 1=距离, 2=角度, 3=ROI
    property int measureTool: 0
    property bool measureMode: false     // 派生: measureTool !== 0

    // ---- 图像尺寸（供状态栏显示） ----
    property int imageResWidth: 800
    property int imageResHeight: 700

    // ---- 按键状态追踪 (W/L + 滚轮) ----
    property bool wKeyPressed: false
    property bool lKeyPressed: false

    // ---- 测量数据模型 (全局共享) ----
    ListModel {
        id: measurementModel
    }

    /**
     * 清除所有测量
     */
    function clearAllMeasurements() {
        measurementModel.clear();
    }

    /**
     * 删除指定测量
     */
    function removeMeasurementAt(index) {
        measurementModel.remove(index);
    }

    // AI 模态名称映射
    readonly property var modalityNames: ["CT 扫描", "MRI 影像", "X-Ray 胸片", "超声检查", "DICOM"]
    readonly property var modalityIcons: ["🫁", "🧠", "🦴", "💓", "📂"]

    // ========================================================================
    // 菜单栏 — macOS 原生集成
    // ========================================================================
    menuBar: MenuBar {
        // ---- 文件 ----
        Menu {
            title: "文件"
            Action {
                text: "打开 DICOM..."
                onTriggered: sidebar.openFileDialog()
            }
            Action {
                text: "导出截图..."
                onTriggered: exportScreenshot()
            }
            MenuSeparator {}
            Action {
                text: "退出"
                onTriggered: Qt.quit()
            }
        }

        // ---- 视图 ----
        Menu {
            title: "视图"
            Action {
                text: "适应窗口"
                onTriggered: { mainWindow.zoomLevel = 1.0; applyZoom() }
            }
            Action {
                text: "实际大小"
                onTriggered: { mainWindow.zoomLevel = 1.0; applyZoom() }
            }
            MenuSeparator {}
            Action {
                text: "全屏"
                onTriggered: toggleFullscreen()
            }
        }

        // ---- 工具 ----
        Menu {
            title: "工具"

            Menu {
                title: "测量工具"
                Action {
                    text: "距离测量"
                    onTriggered: activateMeasure("distance")
                }
                Action {
                    text: "角度测量"
                    onTriggered: activateMeasure("angle")
                }
                Action {
                    text: "ROI 区域"
                    onTriggered: activateMeasure("roi")
                }
                MenuSeparator {}
                Action {
                    text: "关闭测量"
                    onTriggered: deactivateMeasure()
                }
            }

            MenuSeparator {}
            Action {
                text: "重置窗宽窗位"
                onTriggered: resetWindowLevel()
            }
        }

        // ---- 帮助 ----
        Menu {
            title: "帮助"
            Action {
                text: "关于 AI Medical Display"
                onTriggered: aboutDialog.open()
            }
            Action {
                text: "快捷键参考"
                onTriggered: shortcutRefDialog.open()
            }
        }
    }

    // ========================================================================
    // 快捷键系统
    // ========================================================================

    // -- 模态切换 --
    Shortcut { sequence: "Ctrl+1"; onActivated: switchModality(0) }
    Shortcut { sequence: "Ctrl+2"; onActivated: switchModality(1) }
    Shortcut { sequence: "Ctrl+3"; onActivated: switchModality(2) }
    Shortcut { sequence: "Ctrl+4"; onActivated: switchModality(3) }

    // -- 文件操作 --
    Shortcut { sequence: "Ctrl+O"; onActivated: sidebar.openFileDialog() }
    Shortcut {
        sequence: "Ctrl+D"
        onActivated: {
            mainWindow.currentModality = 0
            mainWindow.zoomLevel = 1.0
            mainWindow.windowWidthVal = 400
            mainWindow.windowCenterVal = 40
            mainWindow.brightness = 0.0
            mainWindow.contrast = 1.0
            mainWindow.saturation = 1.0
            mainWindow.measureMode = false
            mainWindow.measureTool = 0
            mainWindow.wlCenter = 40
            mainWindow.wlWidth = 400
            mainWindow.clearAllMeasurements()
            mainWindow.aiModality = mainWindow.modalityNames[0]
        }
    }

    // -- 视图控制 --
    Shortcut {
        sequence: "Ctrl+0"
        onActivated: { mainWindow.zoomLevel = 1.0; applyZoom() }
    }
    Shortcut {
        sequences: [StandardKey.ZoomIn]
        onActivated: { mainWindow.zoomLevel = Math.min(mainWindow.zoomLevel * 1.25, 10.0); applyZoom() }
    }
    Shortcut {
        sequences: [StandardKey.ZoomOut]
        onActivated: { mainWindow.zoomLevel = Math.max(mainWindow.zoomLevel / 1.25, 0.1); applyZoom() }
    }

    // -- 窗宽窗位 (W/L + 滚轮在 ImageViewport 中处理) --
    Shortcut {
        sequence: "Ctrl+R"
        onActivated: resetWindowLevel()
    }

    // -- 工具切换 --
    Shortcut {
        sequence: "M"
        onActivated: {
            if (mainWindow.measureMode) {
                deactivateMeasure()
            } else {
                // 默认切换到距离测量
                activateMeasure("distance")
            }
        }
    }
    Shortcut {
        sequence: "D"
        onActivated: activateMeasure("distance")
    }
    Shortcut {
        sequence: "A"
        onActivated: activateMeasure("angle")
    }
    Shortcut {
        sequence: "R"
        onActivated: activateMeasure("roi")
    }

    // -- 全屏 --
    Shortcut {
        sequence: "F11"
        onActivated: toggleFullscreen()
    }

    // ========================================================================
    // 辅助函数
    // ========================================================================

    /**
     * 切换影像模态
     */
    function switchModality(idx) {
        mainWindow.currentModality = idx
        mainWindow.aiModality = mainWindow.modalityNames[idx]
    }

    /**
     * 激活测量工具
     */
    function activateMeasure(tool) {
        mainWindow.measureMode = true
        switch (tool) {
            case "distance": mainWindow.measureTool = 1; break
            case "angle":    mainWindow.measureTool = 2; break
            case "roi":      mainWindow.measureTool = 3; break
            default:         mainWindow.measureTool = 0; break
        }
    }

    /**
     * 关闭测量工具
     */
    function deactivateMeasure() {
        mainWindow.measureMode = false
        mainWindow.measureTool = 0
    }

    /**
     * 重置窗宽/窗位到原始值
     */
    function resetWindowLevel() {
        if (Dicom.hasImage) {
            mainWindow.wlCenter = mainWindow.wlCenterOrig
            mainWindow.wlWidth = mainWindow.wlWidthOrig
            Dicom.applyWindowLevel(mainWindow.wlCenterOrig, mainWindow.wlWidthOrig)
        } else {
            mainWindow.windowWidthVal = 400
            mainWindow.windowCenterVal = 40
        }
    }

    /**
     * 应用缩放到渲染器
     */
    function applyZoom() {
        // 缩放级别已更新，后续可传递给 Renderer
    }

    /**
     * 切换全屏
     */
    function toggleFullscreen() {
        if (mainWindow.visibility === Window.FullScreen) {
            mainWindow.visibility = Window.Maximized
        } else {
            mainWindow.visibility = Window.FullScreen
        }
    }

    /**
     * 导出当前视口截图
     */
    function exportScreenshot() {
        // 截图暂存（后续版本加保存对话框）
        viewport.grabToImage(function(result) {
            console.log("截图已捕获: " + result.width + "x" + result.height)
        })
    }

    /**
     * 获取当前模态显示名称
     */
    function currentModalityName() {
        if (Dicom.hasImage && mainWindow.currentModality === 4) {
            return "DICOM: " + Dicom.modality
        }
        return mainWindow.modalityNames[mainWindow.currentModality]
    }

    // ========================================================================
    // 按键状态追踪 (用于 W/L + 滚轮交互)
    // ========================================================================
    Item {
        id: keyCapture
        focus: true
        anchors.fill: parent

        Keys.onPressed: function(event) {
            switch (event.key) {
                case Qt.Key_W:
                    mainWindow.wKeyPressed = true
                    event.accepted = true
                    break
                case Qt.Key_L:
                    mainWindow.lKeyPressed = true
                    event.accepted = true
                    break
            }
        }

        Keys.onReleased: function(event) {
            switch (event.key) {
                case Qt.Key_W:
                    mainWindow.wKeyPressed = false
                    event.accepted = true
                    break
                case Qt.Key_L:
                    mainWindow.lKeyPressed = false
                    event.accepted = true
                    break
            }
        }
    }

    // ========================================================================
    // 背景
    // ========================================================================
    Rectangle {
        anchors.fill: parent
        color: colorBg
    }

    // ========================================================================
    // 主布局 (ColumnLayout: 内容区 + 状态栏)
    // ========================================================================
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: spacingSm
        spacing: 0

        // ---- 内容区 ----
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: spacingSm

            // 左侧导航
            Sidebar {
                id: sidebar
                Layout.preferredWidth: 220
                Layout.fillHeight: true
            }

            // 中间影像视口
            ImageViewport {
                id: viewport
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            // 右侧信息面板
            InfoPanel {
                id: infoPanel
                Layout.preferredWidth: 240
                Layout.fillHeight: true
            }
        }

        // ---- 状态栏 ----
        Rectangle {
            id: statusBar
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            Layout.topMargin: spacingSm
            radius: radiusSm
            color: colorSurface
            border.width: 1
            border.color: colorBorder

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: spacingMd
                anchors.rightMargin: spacingMd
                spacing: spacingMd

                // 当前模态
                RowLayout {
                    spacing: spacingXs
                    StatusLabel {
                        label: "模态"
                        value: currentModalityName()
                        accentColor: colorAccent
                    }
                }

                // 分隔线
                StatusDivider {}

                // 图像分辨率
                RowLayout {
                    spacing: spacingXs
                    StatusLabel {
                        label: "分辨率"
                        value: Dicom.hasImage
                            ? Dicom.imageWidth + "×" + Dicom.imageHeight
                            : mainWindow.imageResWidth + "×" + mainWindow.imageResHeight
                    }
                }

                // 分隔线
                StatusDivider {}

                // 窗宽/窗位
                RowLayout {
                    spacing: spacingXs
                    StatusLabel {
                        label: "W/L"
                        value: Dicom.hasImage
                            ? Math.round(mainWindow.wlWidth) + "/" + Math.round(mainWindow.wlCenter)
                            : Math.round(mainWindow.windowWidthVal) + "/" + Math.round(mainWindow.windowCenterVal)
                        accentColor: (mainWindow.wKeyPressed || mainWindow.lKeyPressed) ? colorAccent : "transparent"
                    }
                }

                // 分隔线
                StatusDivider {}

                // 缩放
                RowLayout {
                    spacing: spacingXs
                    StatusLabel {
                        label: "缩放"
                        value: mainWindow.zoomLevel.toFixed(1) + "×"
                    }
                }

                // 测量工具指示
                RowLayout {
                    spacing: spacingXs
                    visible: mainWindow.measureMode
                    StatusLabel {
                        label: "测量"
                        value: measureToolLabel()
                        accentColor: colorSuccess
                    }
                }

                // ---- 弹性空间，右侧信息靠右 ----
                Item { Layout.fillWidth: true }

                // 分隔线
                StatusDivider {}

                // FPS
                RowLayout {
                    spacing: spacingXs
                    StatusLabel {
                        label: "FPS"
                        value: Math.round(mainWindow.renderFps).toString()
                        accentColor: colorInfo
                    }
                }

                // 分隔线
                StatusDivider {}

                // GPU
                RowLayout {
                    spacing: spacingXs
                    StatusLabel {
                        label: "GPU"
                        value: mainWindow.gpuName
                        accentColor: colorInfo
                    }
                }
            }
        }
    }

    // ========================================================================
    // 辅助函数：测量工具标签
    // ========================================================================
    function measureToolLabel() {
        switch (mainWindow.measureTool) {
            case 1: return "距离"
            case 2: return "角度"
            case 3: return "ROI"
            default: return ""
        }
    }

    // ========================================================================
    // 子组件模板
    // ========================================================================
    component StatusLabel: RowLayout {
        property string label
        property string value
        property color accentColor: "transparent"

        spacing: 4
        Label {
            text: label + ":"
            color: mainWindow.colorTextMuted
            font.pixelSize: 10
        }
        Label {
            text: value
            color: accentColor !== "transparent" ? accentColor : mainWindow.colorTextSecondary
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }
    }

    component StatusDivider: Rectangle {
        width: 1
        Layout.preferredHeight: 16
        Layout.alignment: Qt.AlignVCenter
        color: mainWindow.colorBorder
    }

    // ========================================================================
    // 关于对话框
    // ========================================================================
    AboutDialog {
        id: aboutDialog
    }

    // ========================================================================
    // 快捷键参考对话框
    // ========================================================================
    Dialog {
        id: shortcutRefDialog
        title: "快捷键参考"
        width: 520
        height: 520
        modal: true
        standardButtons: Dialog.Ok

        background: Rectangle {
            color: mainWindow.colorSurface
            radius: mainWindow.radiusMd
            border.width: 1
            border.color: mainWindow.colorBorder
        }

        header: Rectangle {
            color: "transparent"
            implicitHeight: 48
            Label {
                anchors.fill: parent
                anchors.margins: 16
                text: "⌨ 快捷键参考"
                color: mainWindow.colorTextPrimary
                font.pixelSize: 16
                font.weight: Font.Bold
                verticalAlignment: Text.AlignVCenter
            }
        }

        contentItem: Flickable {
            clip: true
            contentHeight: shortcutColumn.implicitHeight + 32
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: shortcutColumn
                width: parent.width - 16
                x: 8
                y: 8
                spacing: 8

                ShortcutGroup {
                    title: "模态切换"
                    shortcuts: [
                        { key: "Ctrl+1", desc: "CT 扫描" },
                        { key: "Ctrl+2", desc: "MRI 影像" },
                        { key: "Ctrl+3", desc: "X-Ray 胸片" },
                        { key: "Ctrl+4", desc: "超声检查" }
                    ]
                }

                ShortcutGroup {
                    title: "文件操作"
                    shortcuts: [
                        { key: "Ctrl+O", desc: "打开 DICOM 文件" },
                        { key: "Ctrl+D", desc: "重置到默认测试图案" }
                    ]
                }

                ShortcutGroup {
                    title: "视图控制"
                    shortcuts: [
                        { key: "Ctrl+0", desc: "适应窗口" },
                        { key: "Ctrl+Plus", desc: "放大" },
                        { key: "Ctrl+Minus", desc: "缩小" },
                        { key: "F11", desc: "全屏切换" }
                    ]
                }

                ShortcutGroup {
                    title: "窗宽窗位"
                    shortcuts: [
                        { key: "W + 滚轮", desc: "调节窗宽" },
                        { key: "L + 滚轮", desc: "调节窗位" },
                        { key: "Ctrl+R", desc: "重置窗宽窗位" }
                    ]
                }

                ShortcutGroup {
                    title: "测量工具"
                    shortcuts: [
                        { key: "M", desc: "测量工具 开/关" },
                        { key: "D", desc: "距离测量" },
                        { key: "A", desc: "角度测量" },
                        { key: "R", desc: "ROI 区域" }
                    ]
                }
            }
        }

        // ShortcutGroup 子组件
        component ShortcutGroup: ColumnLayout {
            property string title
            property var shortcuts: []

            Layout.fillWidth: true
            spacing: 2

            Label {
                text: title
                color: mainWindow.colorAccent
                font.pixelSize: 11
                font.weight: Font.DemiBold
                Layout.topMargin: 4
            }

            Repeater {
                model: shortcuts
                delegate: RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 24
                    spacing: 12

                    Rectangle {
                        Layout.preferredWidth: kbdText.implicitWidth + 12
                        Layout.preferredHeight: 20
                        radius: 4
                        color: mainWindow.colorElevated
                        border.width: 1
                        border.color: mainWindow.colorBorder

                        Label {
                            id: kbdText
                            anchors.centerIn: parent
                            text: modelData.key
                            color: mainWindow.colorTextPrimary
                            font.pixelSize: 10
                            font.family: "monospace"
                            font.weight: Font.DemiBold
                        }
                    }

                    Label {
                        text: modelData.desc
                        color: mainWindow.colorTextSecondary
                        font.pixelSize: 11
                    }
                }
            }
        }
    }
    // 截图导出使用 grabToImage（后续版本完善保存对话框）

    // ========================================================================
    // 初始化
    // ========================================================================
    Component.onCompleted: {
        keyCapture.forceActiveFocus()
    }
}
