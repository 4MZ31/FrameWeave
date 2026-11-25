// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The picture alone: the whole window (full screen or not), with the controls showing while the mouse
// moves. Leaving this page does not stop the picture; Disconnect does.
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "page-min"
    color: "black"
    property bool fullscreen: false
    signal requestBack()
    signal requestFullscreen()
    signal requestScreenshot()

    readonly property Item videoViewport: viewport
    readonly property var st: app.stats
    readonly property bool waiting: app.running && Number(st.framesRead || 0) === 0

    Item {
        id: viewport
        objectName: "min-viewport"
        anchors.fill: parent
        PicturePlaceholder { anchors.fill: parent; z: 1; offerSources: false }
    }

    // Controls: shown while the mouse moves, hidden after 2.5 s.
    property bool controlsShown: true
    Timer { id: hide; interval: 2500; running: root.visible; onTriggered: root.controlsShown = false }
    onVisibleChanged: if (visible) { controlsShown = true; hide.restart() }
    // Under the picture (z -1): the picture takes no input, so presses and movement still come here, while
    // what stands on the picture and does take input (the latency readout, PushLatencyBadge.qml) gets its
    // own presses, so it can be dragged and double-clicked here too.
    MouseArea {
        objectName: "min-pointer"
        z: -1
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        onPositionChanged: { root.controlsShown = true; hide.restart() }
        onDoubleClicked: root.requestFullscreen()
        cursorShape: root.controlsShown ? Qt.ArrowCursor : Qt.BlankCursor
    }
    Rectangle {
        objectName: "min-controls"
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.controlsShown ? 14 : -height - 10
        Behavior on y { NumberAnimation { duration: Theme.d(160) } }
        radius: 12
        color: Qt.rgba(0.05, 0.06, 0.08, 0.88)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.13)
        implicitWidth: bar.implicitWidth + 16
        implicitHeight: bar.implicitHeight + 12
        RowLayout {
            id: bar
            x: 8; y: 6
            spacing: 6
            VButton { objectName: "min-back"; iconName: "left"; text: qsTr("返回"); onClicked: root.requestBack() }
            VButton { objectName: "min-stop"; visible: app.running; text: qsTr("断开"); onClicked: app.stop() }
            VButton { objectName: "min-fullscreen"; iconName: "max"; text: root.fullscreen ? qsTr("退出全屏") : qsTr("全屏"); onClicked: root.requestFullscreen() }
            VButton { objectName: "min-screenshot"; visible: app.running; iconName: "camera"; text: qsTr("截图"); onClicked: root.requestScreenshot() }
            // The latency readout over the picture (PushLatencyBadge.qml, in Main.qml); Tab and F3 do the same.
            VButton {
                objectName: "min-stats-toggle"; iconName: "activity"
                readonly property bool on: (veyra.preferences || {}).pushLatencyOverlay === true
                text: on ? qsTr("隐藏统计") : qsTr("显示统计")
                onClicked: veyra.setPreference("pushLatencyOverlay", !on)
            }
        }
    }
}
