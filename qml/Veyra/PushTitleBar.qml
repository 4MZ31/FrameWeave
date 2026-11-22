// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The new design's title bar: the mark and the wordmark, the page's name, what is connected, and the
// window's own buttons. The window is frameless (Main.qml), so this bar is also where it is moved
// from; a double click maximises it, as a Windows title bar does. Legacy has all of this in its dock.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: bar
    objectName: "pushfps-titlebar"
    property string pageName: ""
    property bool maximized: false
    signal requestMinimize()
    signal requestMaximize()
    signal requestClose()
    signal requestMove()
    // Three buttons of 38, the two gaps between them, the row's right margin and the gap before them.
    readonly property real buttonsWidth: 3 * 38 + 2 * 10 + 6 + 5

    color: Theme.card
    topLeftRadius: Theme.rWindow
    topRightRadius: Theme.rWindow

    // Moving the window: the bar left of its three buttons (a press on a button never reaches this
    // item, so two quick presses on one are never a double click on the bar).
    Item {
        objectName: "pushfps-titlebar-drag"
        anchors.fill: parent
        anchors.rightMargin: bar.buttonsWidth
        TapHandler { onDoubleTapped: bar.requestMaximize() }
        DragHandler {
            target: null
            dragThreshold: 4
            onActiveChanged: if (active) bar.requestMove()
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 6
        spacing: 10
        Image {
            Layout.alignment: Qt.AlignVCenter
            source: Theme.logoImage
            sourceSize.width: 48; sourceSize.height: 48
            Layout.preferredWidth: 22; Layout.preferredHeight: 22
            fillMode: Image.PreserveAspectFit
            smooth: true; mipmap: true
        }
        PushWordmark { Layout.alignment: Qt.AlignVCenter; pixelSize: 17 }
        Rectangle { visible: bar.pageName.length > 0; implicitWidth: 1; implicitHeight: 16; color: Theme.stroke2 }
        Text {
            objectName: "pushfps-titlebar-page"
            visible: bar.pageName.length > 0
            text: bar.pageName
            color: Theme.t2
            font.family: Theme.fontUi
            font.pixelSize: 13
            font.variableAxes: Theme.axesMedium
        }
        Item { Layout.fillWidth: true }
        // What is connected: the engine's own state, never a decoration.
        Rectangle {
            objectName: "pushfps-titlebar-source"
            Layout.alignment: Qt.AlignVCenter
            Layout.maximumWidth: Math.max(120, bar.width * 0.34)
            implicitWidth: sourceRow.implicitWidth + 22
            implicitHeight: 28
            radius: 14
            color: Theme.card2
            border.width: 1
            border.color: veyra.hasSource === true ? Qt.rgba(Theme.ok.r, Theme.ok.g, Theme.ok.b, 0.45) : Theme.stroke
            RowLayout {
                id: sourceRow
                anchors.fill: parent
                anchors.leftMargin: 11
                anchors.rightMargin: 11
                spacing: 7
                VDot { off: veyra.hasSource !== true; warn: veyra.captureRecovering === true }
                Text {
                    Layout.fillWidth: true
                    text: veyra.hasSource === true ? (String(veyra.sourceTitle || "").length > 0 ? veyra.sourceTitle : qsTr("已连接")) : qsTr("未打开片源")
                    elide: Text.ElideMiddle
                    color: veyra.hasSource === true ? Theme.t1 : Theme.t3
                    font.family: Theme.fontUi
                    font.pixelSize: 12
                }
            }
        }
        // The window's buttons.
        Repeater {
            model: [
                { id: "min", icon: "minus", tip: qsTr("最小化") },
                { id: "max", icon: "max", tip: bar.maximized ? qsTr("还原") : qsTr("最大化") },
                { id: "close", icon: "x", tip: qsTr("关闭") }
            ]
            delegate: Item {
                id: wbtn
                required property var modelData
                objectName: "pushfps-window-" + modelData.id
                implicitWidth: 38
                implicitHeight: 30
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: modelData.tip
                function act() {
                    if (modelData.id === "min") bar.requestMinimize()
                    else if (modelData.id === "max") bar.requestMaximize()
                    else bar.requestClose()
                }
                Keys.onReturnPressed: act()
                Keys.onSpacePressed: act()
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.rCtl
                    readonly property bool lit: whover.hovered || wbtn.activeFocus
                    color: lit ? (wbtn.modelData.id === "close" ? Qt.rgba(Theme.err.r, Theme.err.g, Theme.err.b, 0.20) : Theme.ink(0.07)) : "transparent"
                    border.width: 1
                    border.color: lit ? (wbtn.modelData.id === "close" ? Qt.rgba(Theme.err.r, Theme.err.g, Theme.err.b, 0.7)
                                                                       : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.7)) : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.d(150) } }
                    Behavior on border.color { ColorAnimation { duration: Theme.d(150) } }
                }
                VIcon {
                    anchors.centerIn: parent
                    name: wbtn.modelData.icon
                    size: 13
                    color: whover.hovered && wbtn.modelData.id === "close" ? Theme.err : whover.hovered ? Theme.t1 : Theme.t2
                }
                HoverHandler { id: whover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: wbtn.act() }
                ToolTip.visible: whover.hovered
                ToolTip.delay: 500
                ToolTip.text: modelData.tip
            }
        }
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.stroke }
}
