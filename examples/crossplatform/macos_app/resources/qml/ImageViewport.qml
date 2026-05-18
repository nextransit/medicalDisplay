import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 影像视口 — C++ 高性能渲染 + QML 显示
 *
 * 架构: C++ ImageRenderer (后台生成) → image://med/ → QML Image 显示
 *
 * 交互功能:
 *   - 鼠标滚轮: 调整窗宽 / Shift+滚轮: 调整窗位
 *   - 鼠标拖拽: 上下=窗位, 左右=窗宽
 *   - 双击: 重置为原始窗宽窗位
 *   - 测量工具: 距离 / 角度 / ROI 矩形
 */

Rectangle {
    id: root
    color: "#000000"
    radius: mainWindow.radiusLg

    property bool animating: false
    Behavior on animating { NumberAnimation { duration: 200 } }
    property int refreshTick: 0
    property int dicomTick: 0

    // ---- 拖拽区域（DICOM 文件接受） ----
    DropArea {
        anchors.fill: parent
        z: 1
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
        source: "image://dicom/current?t=" + root.dicomTick
        onVisibleChanged: {
            if (visible) root.refreshTick++
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

    // C++ 端图像更新后刷新显示（同时刷新 medicalImage 和 dicomOverlay）
    Connections {
        target: Renderer
        function onImageUpdated() {
            root.refreshTick++;
            if (Dicom.hasImage) root.dicomTick++;
        }
    }

    // DICOM 文件加载后捕获原始窗宽窗位
    Connections {
        target: Dicom
        function onFileLoaded() {
            mainWindow.wlCenter = Dicom.windowCenter;
            mainWindow.wlWidth = Dicom.windowWidth;
            mainWindow.wlCenterOrig = Dicom.windowCenter;
            mainWindow.wlWidthOrig = Dicom.windowWidth;
            root.dicomTick++;
        }
    }

    Timer {
        id: modalityTimer
        interval: 320
        onTriggered: {
            root.animating = false;
        }
    }

    // 视口尺寸同步到 C++（300ms 防抖，避免快速缩放时频繁重建 Metal 纹理）
    Timer {
        id: resizeDebounce
        interval: 300
        onTriggered: {
            Renderer.viewportWidth = root.width - 8
            Renderer.viewportHeight = root.height - 8
        }
    }
    onWidthChanged:  resizeDebounce.restart()
    onHeightChanged: resizeDebounce.restart()

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

    // ═══════════════════════════════════════
    // 窗口/窗位 (WL/WW) 鼠标交互
    // ═══════════════════════════════════════

    MouseArea {
        id: wlwwMouse
        anchors.fill: parent
        z: 2
        enabled: mainWindow.measureTool === 0

        // 拖拽时十字光标
        cursorShape: pressed ? Qt.CrossCursor : Qt.ArrowCursor

        property point lastPos: Qt.point(0, 0)
        property real wlDragSensitivity: mainWindow.wlDragSensitivity

        onPressed: {
            lastPos = Qt.point(mouse.x, mouse.y);
        }

        onPositionChanged: {
            var dx = mouse.x - lastPos.x;
            var dy = mouse.y - lastPos.y;
            lastPos = Qt.point(mouse.x, mouse.y);

            if (Dicom.hasImage) {
                // DICOM 模式: 调整窗宽窗位
                // 上下移动 → 窗位 (Window Center/Level)
                var newCenter = mainWindow.wlCenter - dy * wlDragSensitivity;
                // 左右移动 → 窗宽 (Window Width)
                var newWidth = mainWindow.wlWidth + dx * wlDragSensitivity * 3;
                newWidth = Math.max(1, newWidth);

                mainWindow.wlCenter = newCenter;
                mainWindow.wlWidth = newWidth;
                Dicom.applyWindowLevel(newCenter, newWidth);
                root.dicomTick++;
            } else {
                // 测试图案模式: 调整亮度/对比度
                var newBrightness = Math.max(-0.5, Math.min(0.5,
                    mainWindow.brightness - dy * 0.005));
                var newContrast = Math.max(0.5, Math.min(2.0,
                    mainWindow.contrast + dx * 0.005));
                mainWindow.brightness = newBrightness;
                mainWindow.contrast = newContrast;
            }
        }

        // ---- 鼠标滚轮: 窗宽/窗位调节 ----
        onWheel: {
            // delta 标准化 (不同平台滚轮单位不同)
            var delta = wheel.angleDelta.y / 120;

            if (Dicom.hasImage) {
                if (wheel.modifiers & Qt.ShiftModifier) {
                    // Shift+滚轮: 调整窗位
                    var newCenter = mainWindow.wlCenter + delta * 5;
                    mainWindow.wlCenter = newCenter;
                    Dicom.applyWindowLevel(newCenter, mainWindow.wlWidth);
                } else {
                    // 滚轮: 调整窗宽
                    var newWidth = mainWindow.wlWidth + delta * 20;
                    newWidth = Math.max(1, newWidth);
                    mainWindow.wlWidth = newWidth;
                    Dicom.applyWindowLevel(mainWindow.wlCenter, newWidth);
                }
                root.dicomTick++;
            } else {
                // 测试图案: 滚轮调节亮度和对比度
                if (wheel.modifiers & Qt.ShiftModifier) {
                    var nc = Math.max(0.5, Math.min(2.0,
                        mainWindow.contrast + delta * 0.05));
                    mainWindow.contrast = nc;
                } else {
                    var nb = Math.max(-0.5, Math.min(0.5,
                        mainWindow.brightness + delta * 0.02));
                    mainWindow.brightness = nb;
                }
            }
        }

        // ---- 双击重置窗宽窗位 ----
        onDoubleClicked: {
            if (Dicom.hasImage) {
                mainWindow.wlCenter = mainWindow.wlCenterOrig;
                mainWindow.wlWidth = mainWindow.wlWidthOrig;
                Dicom.applyWindowLevel(mainWindow.wlCenterOrig, mainWindow.wlWidthOrig);
                root.dicomTick++;
            } else {
                mainWindow.brightness = 0.0;
                mainWindow.contrast = 1.0;
            }
        }
    }

    // ═══════════════════════════════════════
    // 多帧导航控件（DICOM 多帧时显示）
    // ═══════════════════════════════════════

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 50
        width: frameNavContent.width + 20
        height: 36
        radius: mainWindow.radiusMd
        color: Qt.rgba(0, 0, 0, 0.7)
        visible: Dicom.frameCount > 1
        z: 4

        RowLayout {
            id: frameNavContent
            anchors.centerIn: parent
            spacing: 8

            // 上一帧
            Rectangle {
                width: 28; height: 28; radius: 6
                color: btnPrevMA.pressed ? mainWindow.colorAccentDim : "transparent"
                border.width: 1; border.color: mainWindow.colorBorder

                Label {
                    anchors.centerIn: parent
                    text: "◀"
                    color: mainWindow.colorTextPrimary
                    font.pixelSize: 12
                }

                MouseArea {
                    id: btnPrevMA
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        Dicom.prevFrame()
                        root.dicomTick++
                    }
                }
            }

            // 帧计数器
            Label {
                text: (Dicom.currentFrame + 1) + " / " + Dicom.frameCount
                color: mainWindow.colorAccent
                font.pixelSize: 12
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                Layout.preferredWidth: 60
            }

            // 下一帧
            Rectangle {
                width: 28; height: 28; radius: 6
                color: btnNextMA.pressed ? mainWindow.colorAccentDim : "transparent"
                border.width: 1; border.color: mainWindow.colorBorder

                Label {
                    anchors.centerIn: parent
                    text: "▶"
                    color: mainWindow.colorTextPrimary
                    font.pixelSize: 12
                }

                MouseArea {
                    id: btnNextMA
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        Dicom.nextFrame()
                        root.dicomTick++
                    }
                }
            }
        }
    }

    // ═══════════════════════════════════════
    // 测量工具叠加层
    // ═══════════════════════════════════════

    MeasurementOverlay {
        id: measurementOverlay
        anchors.fill: parent
        z: 3
        toolMode: mainWindow.measureTool
    }

    // ═══════════════════════════════════════
    // Loading 指示器
    // ═══════════════════════════════════════

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

    // ═══════════════════════════════════════
    // 覆盖层: 影像信息 (左下角)
    // ═══════════════════════════════════════

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
                OverlayLabel {
                    label: "W/L"
                    value: mainWindow.wlCenter.toFixed(0) + "/" + mainWindow.wlWidth.toFixed(0)
                }
                OverlayLabel { label: "Zoom"; value: "1.0×" }
                OverlayLabel {
                    label: "Matrix"
                    value: Dicom.hasImage
                        ? (Dicom.imageWidth + "×" + Dicom.imageHeight)
                        : "800×700"
                }
            }
            RowLayout {
                spacing: 16
                OverlayLabel {
                    label: "Patient"
                    value: Dicom.hasImage
                        ? (Dicom.patientName || Dicom.patientId || "—")
                        : "ID: 20240517"
                }
                OverlayLabel {
                    label: "Study"
                    value: Dicom.hasImage
                        ? (Dicom.studyDesc || Dicom.modality || mainWindow.modalityNames[mainWindow.currentModality])
                        : mainWindow.modalityNames[mainWindow.currentModality]
                }
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

    // ═══════════════════════════════════════
    // 右上角标签: AI / DICOM 元数据
    // ═══════════════════════════════════════

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        width: tagContent.width + 20
        height: tagContent.height + 14
        radius: mainWindow.radiusSm
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(0, 0.83, 0.67, 0.7) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0.61, 0.49, 0.7) }
        }
        visible: !Dicom.hasImage

        ColumnLayout {
            id: tagContent
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

    // DICOM 加载时显示文件信息标签
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        width: dicomTagContent.width + 20
        height: dicomTagContent.height + 14
        radius: mainWindow.radiusSm
        color: Qt.rgba(0.18, 0.47, 0.91, 0.7)
        visible: Dicom.hasImage

        ColumnLayout {
            id: dicomTagContent
            anchors.centerIn: parent
            spacing: 0
            Label {
                text: Dicom.modality || "DICOM"
                color: "#ffffff"
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
            Label {
                text: Dicom.hasImage
                    ? (Dicom.patientName || "未命名")
                    : ""
                color: "#ffffff"
                font.pixelSize: 10
                opacity: 0.85
                elide: Text.ElideRight
                Layout.maximumWidth: 160
            }
        }
    }

    // ═══════════════════════════════════════
    // 窗口/窗位 实时数值叠加 (右下角)
    // ═══════════════════════════════════════

    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        width: wlTagContent.width + 16
        height: wlTagContent.height + 12
        radius: mainWindow.radiusSm
        color: Qt.rgba(0, 0, 0, 0.7)
        visible: Dicom.hasImage || mainWindow.measureTool === 0

        ColumnLayout {
            id: wlTagContent
            anchors.centerIn: parent
            spacing: 1

            Label {
                text: "WL: " + mainWindow.wlCenter.toFixed(0)
                color: mainWindow.colorAccent
                font.pixelSize: 10
                font.weight: Font.DemiBold
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: "WW: " + mainWindow.wlWidth.toFixed(0)
                color: "#ffffff"
                font.pixelSize: 10
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: Dicom.hasImage ? "" : "亮度: " + mainWindow.brightness.toFixed(2) + " 对比度: " + mainWindow.contrast.toFixed(2)
                color: mainWindow.colorTextMuted
                font.pixelSize: 8
                Layout.alignment: Qt.AlignHCenter
            }
        }
    }

    // ═══════════════════════════════════════
    // 比例尺
    // ═══════════════════════════════════════

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        width: 80; height: 20
        color: "transparent"
        visible: Dicom.hasImage

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
