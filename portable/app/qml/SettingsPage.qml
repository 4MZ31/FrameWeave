// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Settings: appearance (the Windows build's themes and accents), language and window, files, about.
// Opening this page does not stop the picture.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "page-set"
    color: Theme.bgMid
    // Bring the appearance group into view (the navigation's palette button).
    function showAppearance() { flick.contentY = 0 }

    readonly property var themes: [{ id: "default", tip: qsTr("石墨") }, { id: "light", tip: qsTr("浅色") }, { id: "black", tip: qsTr("纯黑") },
                                   { id: "midnight", tip: qsTr("午夜蓝") }, { id: "plum", tip: qsTr("深紫") }, { id: "forest", tip: qsTr("墨绿") },
                                   { id: "contrast", tip: qsTr("高对比度") }]
    readonly property var accentList: [{ id: "orange", tip: qsTr("PushFPS 绿") }, { id: "white", tip: qsTr("白色光晕") }, { id: "ice", tip: qsTr("冰蓝") },
                                       { id: "blue", tip: qsTr("蓝色") }, { id: "violet", tip: qsTr("紫色") }, { id: "rose", tip: qsTr("玫红") },
                                       { id: "amber", tip: qsTr("琥珀") }]
    readonly property string themeShown: Theme.surfaces[Theme.mode] ? Theme.mode : "default"
    readonly property string accentShown: Theme.accents[Theme.accentName] ? Theme.accentName : "orange"

    component Line: Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.t2
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsBody
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.height + 48
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: VScrollBar {}
        ColumnLayout {
            id: column
            width: Math.min(720, flick.width - 32)
            x: (flick.width - width) / 2
            y: 24
            spacing: 14

            VCard {
                objectName: "set-appearance"
                Layout.fillWidth: true
                PanelTitle { text: qsTr("外观") }
                VRow {
                    objectName: "set-theme-row"
                    label: qsTr("主题")
                    hint: (root.themes.find(t => t.id === root.themeShown) || root.themes[0]).tip
                    hintBelow: true
                    Row {
                        objectName: "set-theme"
                        spacing: 8
                        Repeater {
                            model: root.themes
                            delegate: Rectangle {
                                id: tile
                                required property var modelData
                                readonly property var shown: Theme.surfaceOf(modelData.id)
                                readonly property bool lightLook: modelData.id === "light"
                                readonly property bool on: root.themeShown === modelData.id
                                objectName: "set-theme-" + modelData.id
                                width: 52; height: 38; radius: 8
                                color: shown.bgMid
                                border.width: on || tileHover.hovered ? 2 : 1
                                border.color: on ? Theme.accent : tileHover.hovered ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.7) : Theme.stroke2
                                Behavior on border.color { ColorAnimation { duration: Theme.d(160) } }
                                Rectangle {
                                    x: 7; y: 7; width: parent.width - 14; height: parent.height - 14; radius: 4
                                    color: tile.shown.card2
                                    border.width: tile.modelData.id === "contrast" ? 1 : 0
                                    border.color: "#FFFFFF"
                                    Rectangle { x: 5; y: 6; width: 17; height: 3; radius: 1.5; color: tile.lightLook ? "#17202B" : "#F4F7FA"; opacity: 0.85 }
                                    Rectangle { x: 5; y: 14; width: 11; height: 3; radius: 1.5; color: tile.lightLook ? "#17202B" : "#F4F7FA"; opacity: 0.4 }
                                    Rectangle {
                                        anchors.right: parent.right; anchors.rightMargin: 5; anchors.verticalCenter: parent.verticalCenter
                                        width: 11; height: 7; radius: 3.5
                                        color: tile.lightLook ? Theme.accentPair.light : Theme.accentPair.dark
                                    }
                                }
                                HoverHandler { id: tileHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: veyra.setPreference("pushTheme", tile.modelData.id) }
                                ToolTip.visible: tileHover.hovered
                                ToolTip.delay: 300
                                ToolTip.text: modelData.tip
                                Accessible.role: Accessible.RadioButton
                                Accessible.name: modelData.tip
                                Accessible.checked: on
                            }
                        }
                    }
                }
                VRow {
                    objectName: "set-accent-row"
                    label: qsTr("强调色")
                    hint: (root.accentList.find(a => a.id === root.accentShown) || root.accentList[0]).tip
                    Row {
                        objectName: "set-accent"
                        spacing: 10
                        Repeater {
                            model: root.accentList
                            delegate: Rectangle {
                                id: swatch
                                required property var modelData
                                readonly property bool on: root.accentShown === modelData.id
                                objectName: "set-accent-" + modelData.id
                                width: 26; height: 26; radius: 13
                                color: "transparent"
                                border.width: 2
                                border.color: on ? Theme.t1 : swatchHover.hovered ? Theme.t3 : "transparent"
                                Behavior on border.color { ColorAnimation { duration: Theme.d(160) } }
                                Rectangle {
                                    anchors.fill: parent; anchors.margins: 4; radius: width / 2
                                    color: Theme.light ? Theme.accents[swatch.modelData.id].light : Theme.accents[swatch.modelData.id].dark
                                }
                                HoverHandler { id: swatchHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: veyra.setPreference("accent", swatch.modelData.id) }
                                ToolTip.visible: swatchHover.hovered
                                ToolTip.delay: 300
                                ToolTip.text: modelData.tip
                                Accessible.role: Accessible.RadioButton
                                Accessible.name: modelData.tip
                                Accessible.checked: on
                            }
                        }
                    }
                }
                VRow {
                    label: qsTr("减少动画")
                    VSwitch {
                        objectName: "set-reduced-motion"
                        checked: (veyra.preferences || {}).reducedMotion === true
                        onToggled: on => veyra.setPreference("reducedMotion", on)
                    }
                }
            }

            VCard {
                Layout.fillWidth: true
                PanelTitle { text: qsTr("窗口") }
                VRow {
                    label: qsTr("界面语言")
                    VSelect {
                        objectName: "settings-language"
                        implicitWidth: 200
                        value: app.languageNames[Math.max(0, app.languageCodes.indexOf(app.language))] || ""
                        options: app.languageCodes.map((code, i) => ({ id: code, label: app.languageNames[i] }))
                        onPicked: id => app.language = id
                    }
                }
                VRow {
                    label: qsTr("窗口置顶")
                    VSwitch { objectName: "set-on-top"; checked: app.alwaysOnTop; onToggled: on => app.alwaysOnTop = on }
                }
            }

            VCard {
                Layout.fillWidth: true
                PanelTitle { text: qsTr("文件") }
                VRow {
                    label: qsTr("截图保存位置")
                    VButton { text: qsTr("打开文件夹"); iconName: "folder"; onClicked: app.openScreenshotFolder() }
                }
                VRow {
                    label: qsTr("便携模式")
                    hint: app.portable ? qsTr("已开启：设置和截图保存在程序旁边的文件夹里")
                                       : qsTr("在程序旁边放一个名为 portable.txt 的文件，设置和截图就会保存在那里，删除文件夹即可彻底移除")
                    Text {
                        text: app.portable ? qsTr("开") : qsTr("关")
                        color: app.portable ? Theme.accent : Theme.t3
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                    }
                }
            }

            VCard {
                objectName: "set-about"
                Layout.fillWidth: true
                PanelTitle { text: qsTr("关于") }
                Line { text: "PushFPS " + app.version + " · " + app.platformName }
                Line { text: qsTr("Linux 和 macOS 预览版：补帧、帧节奏修复和实时 HUD 锁定都是 PushFPS 自己的技术，不需要 NVIDIA、AMD 或 Intel 的组件。") }
                Line { text: qsTr("基于开源项目 Veyra（GPL-3.0）打造，感谢原作者 Likely7") }
                Line { color: Theme.t3; font.pixelSize: Theme.fsHint; text: qsTr("PushFPS 以 GPL-3.0 许可发布。使用 Qt 6（LGPL-3.0），视频解码由 Qt 自带的 FFmpeg（LGPL）完成。") }
                Text {
                    text: "github.com/DosukaSOL/PushFPS"
                    color: Theme.accent
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsBody
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: Qt.openUrlExternally("https://github.com/DosukaSOL/PushFPS") }
                }
            }
        }
    }
}
