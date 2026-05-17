import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 右侧面板 — AI 分析结果 + 显示参数控制
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

            // ---- AI 识别标题 ----
            SectionHeader {
                icon: "🤖"
                title: "AI 智能识别"
                subtitle: "Deep Learning Analysis"
            }

            // ---- 识别结果卡片 ----
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

                    // 模态标签
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

                    // 置信度条
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

            // ---- 分隔 ----
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: mainWindow.colorBorder
            }

            // ---- 显示参数 ----
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

    // ---- 子组件 ----

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
}
