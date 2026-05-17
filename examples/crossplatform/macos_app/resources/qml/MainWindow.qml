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
    property int currentModality: 0       // 0:CT 1:MRI 2:XRay 3:超声
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

    // AI 模态名称映射
    readonly property var modalityNames: ["CT 扫描", "MRI 影像", "X-Ray 胸片", "超声检查"]
    readonly property var modalityIcons: ["🫁", "🧠", "🦴", "💓"]

    // ---- 背景 ----
    Rectangle {
        anchors.fill: parent
        color: colorBg
    }

    // ---- 主布局 ----
    RowLayout {
        anchors.fill: parent
        anchors.margins: spacingSm
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
}
