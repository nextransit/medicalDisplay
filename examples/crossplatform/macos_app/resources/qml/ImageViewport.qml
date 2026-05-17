import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 影像视口 — C++ 高性能渲染 + QML 显示
 *
 * 架构: C++ ImageRenderer (后台生成) → image://med/ → QML Image 显示
 */

Rectangle {
    id: root
    color: "#000000"
    radius: mainWindow.radiusLg

    property bool animating: false
    Behavior on animating { NumberAnimation { duration: 200 } }
    property int refreshTick: 0

    // ---- 拖拽区域（DICOM 文件接受） ----
    DropArea {
        anchors.fill: parent
        onEntered: drag.acceptProposedAction()
        onDropped: {
            if (drop.hasUrls && drop.urls.length > 0) {
                Dicom.loadUrl(drop.urls[0])
            }
        }
    }

    // ---- 主影像显示 ----
    Image {
        id: medicalImage
        anchors.fill: parent
        anchors.margins: 4
        opacity: root.animating ? 0.0 : 1.0
        fillMode: Image.PreserveAspectFit
        cache: false
        source: "image://med/current?t=" + root.refreshTick

        Behavior on opacity {
            NumberAnimation { duration: 300; easing.type: Easing.InOutCubic }
        }
    }

    // ---- DICOM 影像叠加层 ----
    Image {
        id: dicomOverlay
        anchors.fill: parent
        anchors.margins: 4
        fillMode: Image.PreserveAspectFit
        visible: Dicom.hasImage
        cache: false
        source: "image://dicom/current"
        onVisibleChanged: {
            if (visible) root.refreshTick++  // 确保刷新
        }
    }

    // ---- 属性同步到 C++ Renderer ----
    Connections {
        target: mainWindow
        function onCurrentModalityChanged() {
            root.animating = true;
            modalityTimer.restart();
            Renderer.modality = mainWindow.currentModality;
        }
        function onBrightnessChanged()   { Renderer.brightness = mainWindow.brightness; }
        function onContrastChanged()     { Renderer.contrast = mainWindow.contrast; }
        function onSaturationChanged()   { Renderer.saturation = mainWindow.saturation; }
        function onGsdfEnabledChanged()  { Renderer.gsdfEnabled = mainWindow.gsdfEnabled; }
    }

    // C++ 端图像更新后刷新显示
    Connections {
        target: Renderer
        function onImageUpdated() { root.refreshTick++; }
    }

    Timer {
        id: modalityTimer
        interval: 320
        onTriggered: {
            root.animating = false;
        }
    }

    // 视口尺寸同步到 C++（用于自适应分辨率渲染）
    onWidthChanged:  Renderer.viewportWidth = width - 8
    onHeightChanged: Renderer.viewportHeight = height - 8

    // 初始同步
    Component.onCompleted: {
        Renderer.viewportWidth = root.width - 8;
        Renderer.viewportHeight = root.height - 8;
        Renderer.modality = mainWindow.currentModality;
        Renderer.brightness = mainWindow.brightness;
        Renderer.contrast = mainWindow.contrast;
        Renderer.saturation = mainWindow.saturation;
        Renderer.gsdfEnabled = mainWindow.gsdfEnabled;
    }

    // ---- Loading 指示器 ----
    Rectangle {
        anchors.centerIn: parent
        width: 80; height: 80; radius: 12
        color: Qt.rgba(0, 0, 0, 0.7)
        visible: Renderer.loading
        opacity: Renderer.loading ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation { duration: 200; easing.type: Easing.InOutCubic }
        }

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 8

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: Renderer.loading
                implicitWidth: 32; implicitHeight: 32
                palette.dark: mainWindow.colorAccent
            }
            Label {
                text: "渲染中..."
                color: mainWindow.colorTextSecondary
                font.pixelSize: 10
                Layout.alignment: Qt.AlignHCenter
            }
        }
    }

    // ---- 覆盖层: DICOM 信息 ----
    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 12
        width: infoContent.width + 24
        height: infoContent.height + 16
        radius: mainWindow.radiusSm
        color: Qt.rgba(0, 0, 0, 0.65)

        ColumnLayout {
            id: infoContent
            anchors.centerIn: parent
            spacing: 2
            RowLayout {
                spacing: 16
                OverlayLabel { label: "W/L"; value: "40/400" }
                OverlayLabel { label: "Zoom"; value: "1.0×" }
                OverlayLabel { label: "Matrix"; value: "800×700" }
            }
            RowLayout {
                spacing: 16
                OverlayLabel { label: "Patient"; value: "ID: 20240517" }
                OverlayLabel { label: "Study"; value: mainWindow.modalityNames[mainWindow.currentModality] }
            }
        }
    }

    component OverlayLabel: RowLayout {
        property string label
        property string value
        spacing: 4
        Label {
            text: label
            color: mainWindow.colorAccent
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }
        Label {
            text: value
            color: "#ffffff"
            font.pixelSize: 10
        }
    }

    // ---- 右上角 AI 标签 ----
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        width: aiTagContent.width + 20
        height: aiTagContent.height + 14
        radius: mainWindow.radiusSm
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(0, 0.83, 0.67, 0.7) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0.61, 0.49, 0.7) }
        }
        ColumnLayout {
            id: aiTagContent
            anchors.centerIn: parent
            spacing: 0
            Label {
                text: "AI 识别: " + mainWindow.aiModality
                color: "#ffffff"
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
            Label {
                text: "置信度: " + Math.round(mainWindow.aiConfidence * 100) + "%"
                color: "#ffffff"
                font.pixelSize: 10
                opacity: 0.85
            }
        }
    }

    // ---- 比例尺 ----
    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        width: 80; height: 20
        color: "transparent"
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 8
            anchors.horizontalCenter: parent.horizontalCenter
            width: 60; height: 1
            color: "#ffffff"
            opacity: 0.6
        }
        Label {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            text: "5 cm"
            color: "#ffffff"
            font.pixelSize: 9
            opacity: 0.6
        }
    }
}
