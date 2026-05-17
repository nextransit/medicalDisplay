import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 右侧面板 — AI 分析结果 + 显示参数控制 + 测量工具
 */

Rectangle {
    id: root
    color: mainWindow.colorSurface
    radius: mainWindow.radiusLg

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: "#00d4aa22" }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: mainWindow.spacingMd
        contentHeight: contentColumn.implicitHeight
        clip: true
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            active: true
        }

        ColumnLayout {
            id: contentColumn
            width: parent.width
            spacing: mainWindow.spacingMd

            // ═══════════════════════════════════════
            // DICOM 元数据 (当加载 DICOM 文件时)
            // ═══════════════════════════════════════

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Dicom.hasImage ? implicitHeight : 0
                visible: Dicom.hasImage
                implicitHeight: dicomMetaSection.implicitHeight

                ColumnLayout {
                    id: dicomMetaSection
                    width: parent.width
                    spacing: mainWindow.spacingMd

                    SectionHeader {
                        icon: "📋"
                        title: "DICOM 信息"
                        subtitle: "DICOM Metadata"
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: dicomMetaGrid.implicitHeight + 24
                        radius: mainWindow.radiusMd
                        color: mainWindow.colorElevated
                        border.width: 1
                        border.color: Qt.rgba(0.18, 0.47, 0.91, 0.15)

                        GridLayout {
                            id: dicomMetaGrid
                            anchors.fill: parent
                            anchors.margins: 14
                            columns: 2
                            rowSpacing: 6
                            columnSpacing: 12

                            MetaField { label: "患者姓名"; value: Dicom.patientName || "—" }
                            MetaField { label: "患者 ID"; value: Dicom.patientId || "—" }
                            MetaField { label: "模态"; value: Dicom.modality || "—" }
                            MetaField { label: "检查描述"; value: Dicom.studyDesc || "—" }
                            MetaField { label: "图像尺寸"; value: Dicom.imageWidth + " × " + Dicom.imageHeight }
                            MetaField { label: "原始窗宽"; value: mainWindow.wlWidthOrig.toFixed(0) }
                            MetaField { label: "原始窗位"; value: mainWindow.wlCenterOrig.toFixed(0) }
                            MetaField { label: "当前窗宽"; value: mainWindow.wlWidth.toFixed(0) }
                            MetaField { label: "当前窗位"; value: mainWindow.wlCenter.toFixed(0) }
                        }
                    }

                    // 分隔
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: mainWindow.colorBorder
                    }
                }
            }

            // ═══════════════════════════════════════
            // AI 识别 (仅在无 DICOM 时显示)
            // ═══════════════════════════════════════

            ColumnLayout {
                Layout.fillWidth: true
                visible: !Dicom.hasImage
                spacing: mainWindow.spacingMd

                SectionHeader {
                    icon: "🤖"
                    title: "AI 智能识别"
                    subtitle: "Deep Learning Analysis"
                }

                // 识别结果卡片
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 100
                    radius: mainWindow.radiusMd
                    color: mainWindow.colorElevated
                    border.width: 1
                    border.color: Qt.rgba(0, 0.83, 0.67, 0.15)

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 6

                        RowLayout {
                            spacing: 8
                            Rectangle {
                                width: 40; height: 40; radius: 10
                                color: Qt.rgba(0, 0.83, 0.67, 0.15)

                                Label {
                                    anchors.centerIn: parent
                                    text: mainWindow.modalityIcons[mainWindow.currentModality]
                                    font.pixelSize: 22
                                }
                            }
                            ColumnLayout {
                                spacing: 1
                                Label {
                                    text: mainWindow.aiModality
                                    color: mainWindow.colorAccent
                                    font.pixelSize: 18
                                    font.weight: Font.Bold
                                }
                                Label {
                                    text: "识别模态"
                                    color: mainWindow.colorTextMuted
                                    font.pixelSize: 10
                                }
                            }
                        }

                        RowLayout {
                            spacing: 8
                            Label {
                                text: "置信度"
                                color: mainWindow.colorTextSecondary
                                font.pixelSize: 12
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 6
                                radius: 3
                                color: mainWindow.colorBorder

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: parent.width * mainWindow.aiConfidence
                                    radius: 3
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0.0; color: mainWindow.colorAccentDim }
                                        GradientStop { position: 1.0; color: mainWindow.colorAccent }
                                    }

                                    Behavior on width {
                                        NumberAnimation { duration: 400; easing.type: Easing.OutCubic }
                                    }
                                }
                            }

                            Label {
                                text: Math.round(mainWindow.aiConfidence * 100) + "%"
                                color: mainWindow.colorSuccess
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }

                // 分隔
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: mainWindow.colorBorder
                }
            }

            // ═══════════════════════════════════════
            // 测量工具
            // ═══════════════════════════════════════

            SectionHeader {
                icon: "📏"
                title: "测量工具"
                subtitle: "Measurement Tools"
            }

            // 工具切换按钮
            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                ToolButton {
                    text: "⊘"
                    tooltip: "无 / 窗宽窗位调节"
                    checked: mainWindow.measureTool === 0
                    onClicked: mainWindow.measureTool = 0
                }
                ToolButton {
                    text: "←→"
                    tooltip: "距离测量 (点击两点)"
                    checked: mainWindow.measureTool === 1
                    onClicked: mainWindow.measureTool = 1
                }
                ToolButton {
                    text: "∠"
                    tooltip: "角度测量 (点击三点: 边-顶点-边)"
                    checked: mainWindow.measureTool === 2
                    onClicked: mainWindow.measureTool = 2
                }
                ToolButton {
                    text: "▭"
                    tooltip: "ROI 矩形 (拖拽绘制)"
                    checked: mainWindow.measureTool === 3
                    onClicked: mainWindow.measureTool = 3
                }
            }

            // 当前模式提示
            Label {
                Layout.fillWidth: true
                text: {
                    switch (mainWindow.measureTool) {
                        case 0: return "窗宽窗位调节模式 — 拖拽/滚轮调节";
                        case 1: return "距离测量 — 点击影像上的两点";
                        case 2: return "角度测量 — 依次点击三条边的三个点";
                        case 3: return "ROI 测量 — 拖拽绘制矩形区域";
                        default: return "";
                    }
                }
                color: mainWindow.colorTextMuted
                font.pixelSize: 9
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            // 测量结果列表
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(Math.max(measurementModel.count * 52, 40), 200)
                radius: mainWindow.radiusMd
                color: mainWindow.colorElevated
                visible: measurementModel.count > 0

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    // 列表标题 + 清除按钮
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            text: "测量结果 (" + measurementModel.count + ")"
                            color: mainWindow.colorTextPrimary
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }

                        Item { Layout.fillWidth: true }

                        // 清除按钮
                        Rectangle {
                            width: clearLabel.implicitWidth + 16
                            height: 22
                            radius: mainWindow.radiusSm
                            color: clearMouse.containsMouse
                                ? Qt.rgba(0.93, 0.27, 0.27, 0.2)
                                : Qt.rgba(0.93, 0.27, 0.27, 0.08)

                            Behavior on color {
                                ColorAnimation { duration: 150 }
                            }

                            Label {
                                id: clearLabel
                                anchors.centerIn: parent
                                text: "清除全部"
                                color: clearMouse.containsMouse ? mainWindow.colorDanger : mainWindow.colorTextMuted
                                font.pixelSize: 10
                            }

                            MouseArea {
                                id: clearMouse
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                hoverEnabled: true
                                onClicked: mainWindow.clearAllMeasurements()
                            }
                        }
                    }

                    // 测量列表
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        ScrollBar.vertical.policy: measurementModel.count > 3
                            ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff

                        ColumnLayout {
                            width: parent.width
                            spacing: 2

                            Repeater {
                                id: measureRepeater
                                model: measurementModel

                                delegate: Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 36
                                    radius: mainWindow.radiusSm
                                    color: measureItemMouse.containsMouse
                                        ? Qt.rgba(1, 1, 1, 0.04) : "transparent"

                                    Behavior on color {
                                        ColorAnimation { duration: 150 }
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 6
                                        anchors.rightMargin: 4
                                        spacing: 6

                                        // 类型图标
                                        Label {
                                            text: {
                                                switch (model.type) {
                                                    case "distance": return "←→";
                                                    case "angle": return "∠";
                                                    case "roi": return "▭";
                                                    default: return "●";
                                                }
                                            }
                                            color: model.lineColor || mainWindow.colorAccent
                                            font.pixelSize: 12
                                            Layout.preferredWidth: 20
                                            horizontalAlignment: Text.AlignHCenter
                                        }

                                        // 数值
                                        Label {
                                            text: model.value
                                            color: mainWindow.colorTextPrimary
                                            font.pixelSize: 11
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                        }

                                        // 删除按钮
                                        Rectangle {
                                            width: 20; height: 20
                                            radius: 4
                                            color: deleteMouse.containsMouse
                                                ? Qt.rgba(0.93, 0.27, 0.27, 0.15)
                                                : "transparent"

                                            Behavior on color {
                                                ColorAnimation { duration: 150 }
                                            }

                                            Label {
                                                anchors.centerIn: parent
                                                text: "×"
                                                color: deleteMouse.containsMouse
                                                    ? mainWindow.colorDanger
                                                    : mainWindow.colorTextMuted
                                                font.pixelSize: 14
                                                font.weight: Font.Bold
                                            }

                                            MouseArea {
                                                id: deleteMouse
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                hoverEnabled: true
                                                onClicked: mainWindow.removeMeasurementAt(model.index)
                                            }
                                        }
                                    }

                                    MouseArea {
                                        id: measureItemMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.NoButton
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 空状态
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                radius: mainWindow.radiusSm
                color: "transparent"
                visible: measurementModel.count === 0

                Label {
                    anchors.centerIn: parent
                    text: "暂无测量 — 选择工具开始测量"
                    color: mainWindow.colorTextMuted
                    font.pixelSize: 10
                }
            }

            // ---- 分隔 ----
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: mainWindow.colorBorder
            }

            // ═══════════════════════════════════════
            // 显示参数
            // ═══════════════════════════════════════

            SectionHeader {
                icon: "🖥️"
                title: "显示参数"
                subtitle: "Image Adjustment"
            }

            // 亮度
            ParamSlider {
                label: "亮度"
                icon: "☀️"
                value: mainWindow.brightness
                from: -0.5; to: 0.5
                onParamChanged: mainWindow.brightness = value
            }

            // 对比度
            ParamSlider {
                label: "对比度"
                icon: "◐"
                value: mainWindow.contrast
                from: 0.5; to: 2.0
                onParamChanged: mainWindow.contrast = value
            }

            // 饱和度
            ParamSlider {
                label: "饱和度"
                icon: "🎨"
                value: mainWindow.saturation
                from: 0.0; to: 2.0
                onParamChanged: mainWindow.saturation = value
            }

            // ---- 高级功能 ----
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                Layout.topMargin: mainWindow.spacingSm
                color: mainWindow.colorBorder
            }

            SectionHeader {
                icon: "⚙️"
                title: "高级功能"
                subtitle: "Advanced Settings"
            }

            // GSDF 开关
            ToggleRow {
                label: "GSDF 感知校准"
                description: "DICOM Part 14 标准"
                checked: mainWindow.gsdfEnabled
                onCheckedChanged: mainWindow.gsdfEnabled = checked
            }

            // 无血模式
            ToggleRow {
                label: "无血化显示"
                description: "抑制红色组织增强结构"
                checked: mainWindow.bloodlessMode
                onCheckedChanged: mainWindow.bloodlessMode = checked
            }

            // ---- 性能指标 ----
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                Layout.topMargin: mainWindow.spacingSm
                color: mainWindow.colorBorder
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 72
                radius: mainWindow.radiusMd
                color: mainWindow.colorElevated

                GridLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    columns: 2
                    rowSpacing: 8
                    columnSpacing: 16

                    MetricBadge {
                        label: "渲染引擎"
                        value: "Metal GPU"
                    }
                    MetricBadge {
                        label: "帧率"
                        value: Math.round(mainWindow.renderFps) + " fps"
                    }
                    MetricBadge {
                        label: "分辨率"
                        value: "1920×1080"
                    }
                    MetricBadge {
                        label: "延迟"
                        value: "< 8 ms"
                    }
                }
            }
        }
    }

    // ═══════════════════════════════════════
    // 子组件
    // ═══════════════════════════════════════

    component SectionHeader: RowLayout {
        property string icon
        property string title
        property string subtitle

        Layout.fillWidth: true
        spacing: 8

        Label {
            text: icon
            font.pixelSize: 14
        }
        ColumnLayout {
            spacing: 0
            Label {
                text: title
                color: mainWindow.colorTextPrimary
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Label {
                text: subtitle
                color: mainWindow.colorTextMuted
                font.pixelSize: 9
            }
        }
    }

    component ParamSlider: ColumnLayout {
        id: ctrlRoot
        property string label
        property string icon
        property alias value: slider.value
        property alias from: slider.from
        property alias to: slider.to
        signal paramChanged(real val)

        Layout.fillWidth: true
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                text: icon + " " + label
                color: mainWindow.colorTextSecondary
                font.pixelSize: 11
                Layout.preferredWidth: 70
            }

            Slider {
                id: slider
                Layout.fillWidth: true
                from: parent.from; to: parent.to
                value: 0.0
                stepSize: 0.01

                onValueChanged: ctrlRoot.paramChanged(value)

                background: Rectangle {
                    x: slider.leftPadding
                    y: slider.topPadding + slider.availableHeight / 2 - 2
                    implicitWidth: 200; implicitHeight: 4
                    width: slider.availableWidth; height: implicitHeight
                    radius: 2
                    color: mainWindow.colorBorder

                    Rectangle {
                        width: slider.visualPosition * parent.width
                        height: parent.height
                        radius: 2
                        color: mainWindow.colorAccent
                    }
                }

                handle: Rectangle {
                    x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                    y: slider.topPadding + slider.availableHeight / 2 - height / 2
                    implicitWidth: 14; implicitHeight: 14
                    radius: 7
                    color: slider.pressed ? mainWindow.colorAccent : mainWindow.colorTextPrimary
                    border.width: 2
                    border.color: mainWindow.colorAccent

                    Behavior on color {
                        ColorAnimation { duration: 150 }
                    }
                }
            }

            Label {
                text: slider.value.toFixed(2)
                color: mainWindow.colorAccent
                font.pixelSize: 11
                font.weight: Font.DemiBold
                Layout.preferredWidth: 40
                horizontalAlignment: Text.AlignRight
            }
        }
    }

    component ToggleRow: Rectangle {
        property string label
        property string description
        property alias checked: toggleSwitch.checked

        Layout.fillWidth: true
        Layout.preferredHeight: 44
        radius: mainWindow.radiusSm
        color: toggleSwitch.checked ? Qt.rgba(0, 0.83, 0.67, 0.06) : "transparent"

        Behavior on color {
            ColorAnimation { duration: 200 }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    text: label
                    color: mainWindow.colorTextPrimary
                    font.pixelSize: 12
                }
                Label {
                    text: description
                    color: mainWindow.colorTextMuted
                    font.pixelSize: 9
                }
            }

            Switch {
                id: toggleSwitch

                indicator: Rectangle {
                    implicitWidth: 36; implicitHeight: 20
                    radius: 10
                    color: checked ? mainWindow.colorAccent : mainWindow.colorBorder

                    Behavior on color {
                        ColorAnimation { duration: 200 }
                    }

                    Rectangle {
                        width: 16; height: 16; radius: 8
                        color: "#ffffff"
                        anchors.verticalCenter: parent.verticalCenter
                        x: checked ? parent.width - width - 2 : 2

                        Behavior on x {
                            NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
                        }
                    }
                }
            }
        }
    }

    component MetricBadge: ColumnLayout {
        property string label
        property string value

        Layout.fillWidth: true
        spacing: 1

        Label {
            text: label
            color: mainWindow.colorTextMuted
            font.pixelSize: 9
        }
        Label {
            text: value
            color: mainWindow.colorTextPrimary
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
    }

    component MetaField: ColumnLayout {
        property string label
        property string value

        Layout.fillWidth: true
        spacing: 0

        Label {
            text: label
            color: mainWindow.colorTextMuted
            font.pixelSize: 9
        }
        Label {
            text: value
            color: mainWindow.colorTextPrimary
            font.pixelSize: 11
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }

    component ToolButton: Rectangle {
        property string text
        property string tooltip
        property alias checked: toolBtnArea.checked
        signal clicked()

        Layout.fillWidth: true
        Layout.preferredHeight: 36
        radius: mainWindow.radiusSm

        // 根据类型使用不同颜色
        property color accentColor: {
            switch (text) {
                case "⊘": return mainWindow.colorTextSecondary;
                case "←→": return "#00d4aa";    // 距离: 青色
                case "∠": return "#f59e0b";     // 角度: 琥珀
                case "▭": return "#3b82f6";     // ROI: 蓝色
                default: return mainWindow.colorAccent;
            }
        }

        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {
                position: 0.0
                color: checked ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.15)
                               : mainWindow.colorElevated
            }
            GradientStop {
                position: 1.0
                color: checked ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.08)
                               : mainWindow.colorElevated
            }
        }

        border.width: checked ? 1 : 0
        border.color: checked ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.4)
                              : "transparent"

        Behavior on gradient {
            ColorAnimation { duration: 200; easing.type: Easing.OutCubic }
        }
        Behavior on border.color {
            ColorAnimation { duration: 200; easing.type: Easing.OutCubic }
        }

        Label {
            anchors.centerIn: parent
            text: parent.text
            color: parent.checked ? parent.accentColor : mainWindow.colorTextSecondary
            font.pixelSize: 14
            font.weight: parent.checked ? Font.Bold : Font.Normal

            Behavior on color {
                ColorAnimation { duration: 200; easing.type: Easing.OutCubic }
            }
        }

        MouseArea {
            id: toolBtnArea
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            property bool checked: false
            onClicked: parent.clicked()
        }

        // Tooltip
        ToolTip {
            visible: toolBtnArea.containsMouse
            text: parent.tooltip
            delay: 500
            font.pixelSize: 11
        }
    }
}
