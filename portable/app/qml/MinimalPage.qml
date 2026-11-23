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

    // Statistics (Tab).
    Rectangle {
        objectName: "min-stats"
        visible: app.showStats && app.running && !root.waiting
        x: 16; y: 16
        radius: 8
        color: Qt.rgba(0, 0, 0, 0.62)
        implicitWidth: stats.implicitWidth + 20
        implicitHeight: stats.implicitHeight + 14
        Column {
            id: stats
            x: 10; y: 7
            spacing: 2
            Text {
                color: "white"; font.pixelSize: 13; font.family: Theme.fontUi; font.bold: true
                text: qsTr("游戏 %1 FPS → 输出 %2 FPS").arg(Math.round(root.st.contentFps || 0)).arg(Math.round(root.st.outputFps || 0))
            }
            Text {
                color: Qt.rgba(1, 1, 1, 0.75); font.pixelSize: 12; font.family: Theme.fontUi
                text: qsTr("信号 %1 Hz · 延迟 %2 ms · 运动估计 %3 ms").arg(Math.round(root.st.transportFps || 0))
                          .arg(Math.round(root.st.latencyMs || 0)).arg((root.st.estimateMs || 0).toFixed(1))
            }
            Text {
                visible: app.pacingRepair
                color: Qt.rgba(1, 1, 1, 0.75); font.pixelSize: 12; font.family: Theme.fontUi
                text: qsTr("正在修复：%1% 的画面重新对齐").arg(Math.round((root.st.pacingShare || 0) * 100))
            }
            Text {
                visible: (root.st.marginMs || 0) >= 1
                color: Qt.rgba(1, 1, 1, 0.75); font.pixelSize: 12; font.family: Theme.fontUi
                text: qsTr("这台电脑的额外等待：%1 ms").arg(Math.round(root.st.marginMs || 0))
            }
        }
    }

    // Controls: shown while the mouse moves, hidden after 2.5 s.
    property bool controlsShown: true
    Timer { id: hide; interval: 2500; running: root.visible; onTriggered: root.controlsShown = false }
    onVisibleChanged: if (visible) { controlsShown = true; hide.restart() }
    MouseArea {
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
            VButton { objectName: "min-stats-toggle"; iconName: "activity"; text: app.showStats ? qsTr("隐藏统计") : qsTr("显示统计"); onClicked: app.showStats = !app.showStats }
        }
    }
}
