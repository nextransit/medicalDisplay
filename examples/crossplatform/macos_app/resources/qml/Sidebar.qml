import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 侧边栏 — 模态选择 + 系统状态
 */

Rectangle {
    id: root
    color: mainWindow.colorSurface
    radius: mainWindow.radiusLg

    // 微妙的顶部光晕
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.3; color: "#00d4aa22" }
            GradientStop { position: 0.7; color: "#00d4aa22" }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: mainWindow.spacingMd
        spacing: mainWindow.spacingSm

        // ---- 标题区域 ----
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 48

            RowLayout {
                anchors.fill: parent
                spacing: mainWindow.spacingSm

                Rectangle {
                    width: 32; height: 32; radius: 8
                    color: mainWindow.colorAccent
                    opacity: 0.15

                    Label {
                        anchors.centerIn: parent
                        text: "⚕"
                        font.pixelSize: 16
                    }
                }

                ColumnLayout {
                    spacing: 0
                    Label {
                        text: "影像模态"
                        color: mainWindow.colorTextPrimary
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: "Modality Selection"
                        color: mainWindow.colorTextMuted
                        font.pixelSize: 10
                    }
                }
            }
        }

        // 分隔线
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            Layout.bottomMargin: mainWindow.spacingSm
            color: mainWindow.colorBorder
        }

        // ---- 模态按钮列表 ----
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Repeater {
                model: ListModel {
                    ListElement { name: "CT 扫描"; icon: "🫁"; desc: "计算机断层扫描"; idx: 0 }
                    ListElement { name: "MRI 影像"; icon: "🧠"; desc: "磁共振成像"; idx: 1 }
                    ListElement { name: "X-Ray"; icon: "🦴"; desc: "数字 X 射线"; idx: 2 }
                    ListElement { name: "超声"; icon: "💓"; desc: "超声影像"; idx: 3 }
                }

                delegate: Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56

                    Rectangle {
                        anchors.fill: parent
                        radius: mainWindow.radiusMd
                        color: mainWindow.currentModality === idx
                               ? Qt.rgba(0, 0.83, 0.67, 0.12)
                               : "transparent"
                        border.width: mainWindow.currentModality === idx ? 1 : 0
                        border.color: mainWindow.currentModality === idx
                                      ? Qt.rgba(0, 0.83, 0.67, 0.3)
                                      : "transparent"

                        Behavior on color {
                            ColorAnimation { duration: 200; easing.type: Easing.OutCubic }
                        }
                        Behavior on border.color {
                            ColorAnimation { duration: 200; easing.type: Easing.OutCubic }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 12

                            // 图标
                            Rectangle {
                                width: 36; height: 36; radius: 8
                                color: mainWindow.currentModality === idx
                                       ? mainWindow.colorAccent
                                       : mainWindow.colorElevated
                                opacity: mainWindow.currentModality === idx ? 0.2 : 1.0

                                Behavior on color {
                                    ColorAnimation { duration: 200; easing.type: Easing.OutCubic }
                                }

                                Label {
                                    anchors.centerIn: parent
                                    text: icon
                                    font.pixelSize: 18
                                }
                            }

                            ColumnLayout {
                                spacing: 1

                                Label {
                                    text: name
                                    color: mainWindow.currentModality === idx
                                           ? mainWindow.colorAccent
                                           : mainWindow.colorTextPrimary
                                    font.pixelSize: 13
                                    font.weight: mainWindow.currentModality === idx
                                                 ? Font.DemiBold : Font.Normal

                                    Behavior on color {
                                        ColorAnimation { duration: 200; easing.type: Easing.OutCubic }
                                    }
                                }
                                Label {
                                    text: desc
                                    color: mainWindow.colorTextMuted
                                    font.pixelSize: 10
                                }
                            }

                            Item { Layout.fillWidth: true }

                            // 选中指示器
                            Rectangle {
                                visible: mainWindow.currentModality === idx
                                width: 4; height: 24; radius: 2
                                color: mainWindow.colorAccent
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                mainWindow.currentModality = idx
                                mainWindow.aiModality = mainWindow.modalityNames[idx]
                            }
                        }
                    }
                }
            }
        }

        // ---- 弹性空间 ----
        Item { Layout.fillHeight: true }

        // ---- 底部状态区 ----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            Layout.topMargin: mainWindow.spacingMd
            radius: mainWindow.radiusMd
            color: mainWindow.colorElevated

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                // GSDF 状态
                RowLayout {
                    spacing: 8
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: mainWindow.gsdfEnabled ? mainWindow.colorSuccess : mainWindow.colorTextMuted

                        SequentialAnimation on opacity {
                            running: mainWindow.gsdfEnabled
                            loops: Animation.Infinite
                            NumberAnimation { from: 0.6; to: 1.0; duration: 1000 }
                            NumberAnimation { from: 1.0; to: 0.6; duration: 1000 }
                        }
                    }
                    Label {
                        text: mainWindow.gsdfEnabled ? "GSDF 已校准" : "GSDF 关闭"
                        color: mainWindow.gsdfEnabled
                               ? mainWindow.colorSuccess
                               : mainWindow.colorTextMuted
                        font.pixelSize: 11
                    }
                }

                // GPU 状态
                RowLayout {
                    spacing: 8
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: mainWindow.colorInfo
                    }
                    ColumnLayout {
                        spacing: 0
                        Label {
                            text: "GPU: " + mainWindow.gpuName
                            color: mainWindow.colorTextSecondary
                            font.pixelSize: 10
                        }
                        Label {
                            text: Math.round(mainWindow.renderFps) + " fps · Metal"
                            color: mainWindow.colorTextMuted
                            font.pixelSize: 9
                        }
                    }
                }
            }
        }
    }
}
