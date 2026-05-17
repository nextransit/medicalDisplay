import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 关于对话框 — AI Medical Display
 *
 * 显示应用名、版本号、技术栈、构建时间、版权信息
 */

Dialog {
    id: aboutDialog
    title: "关于 AI Medical Display"
    width: 420
    height: 340
    modal: true
    standardButtons: Dialog.Ok

    background: Rectangle {
        color: mainWindow.colorSurface
        radius: mainWindow.radiusMd
        border.width: 1
        border.color: mainWindow.colorBorder
    }

    contentItem: ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: mainWindow.spacingMd

        // ---- 应用图标 + 名称 ----
        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: mainWindow.spacingSm

            // 图标
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 64
                height: 64
                radius: 16
                color: mainWindow.colorAccent
                opacity: 0.15

                Label {
                    anchors.centerIn: parent
                    text: "⚕"
                    font.pixelSize: 32
                }
            }

            // 应用名
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: "AI Medical Display"
                color: mainWindow.colorAccent
                font.pixelSize: 20
                font.weight: Font.Bold
            }

            // 版本号
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: "版本 " + (typeof appVersion !== "undefined" ? appVersion : "2.0.0")
                color: mainWindow.colorTextSecondary
                font.pixelSize: 13
            }

            // 副标题
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: "智能医学影像显示系统"
                color: mainWindow.colorTextMuted
                font.pixelSize: 11
            }
        }

        // ---- 分隔线 ----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: mainWindow.colorBorder
        }

        // ---- 技术信息 ----
        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 6

            AboutRow { label: "技术栈"; value: "Qt 6 + QML + Metal GPU + C++17" }
            AboutRow { label: "渲染引擎"; value: "Metal Compute Shader" }
            AboutRow { label: "DICOM 支持"; value: "Part 10 文件格式解析" }
            AboutRow { label: "构建日期"; value: "2025-05-17" }
        }

        // ---- 分隔线 ----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: mainWindow.colorBorder
        }

        // ---- 版权信息 ----
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "© 2025 MedicalDisplay Team"
            color: mainWindow.colorTextMuted
            font.pixelSize: 11
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "保留所有权利"
            color: mainWindow.colorTextMuted
            font.pixelSize: 10
        }
    }

    // ---- 子组件 ----
    component AboutRow: RowLayout {
        property string label
        property string value

        Layout.fillWidth: true
        spacing: 12

        Label {
            text: label
            color: mainWindow.colorTextMuted
            font.pixelSize: 11
            Layout.preferredWidth: 80
            horizontalAlignment: Text.AlignRight
        }
        Label {
            text: value
            color: mainWindow.colorTextPrimary
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
    }
}
