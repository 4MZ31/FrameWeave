// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Where the picture stands while nothing plays, or while the first frame has not arrived: says so, and
// offers the way to a picture.
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: holder
    property bool offerSources: true
    signal openFile()
    readonly property bool waiting: app.running && Number(app.stats.framesRead || 0) === 0
    color: "black"
    visible: !app.running || waiting

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 420)
        spacing: 12
        VIcon {
            Layout.alignment: Qt.AlignHCenter
            name: holder.waiting ? "clock" : "video"
            size: 30
            color: Theme.t3
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: holder.waiting ? qsTr("正在等待画面…") : qsTr("未打开片源")
            color: Theme.onVideo
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsH3
        }
        RowLayout {
            visible: !holder.waiting && holder.offerSources
            Layout.alignment: Qt.AlignHCenter
            spacing: 8
            VButton {
                objectName: "placeholder-connect"
                text: qsTr("连接")
                primary: true
                enabled: app.videoDevices.length > 0
                onClicked: app.startCamera()
            }
            VButton { text: qsTr("打开视频"); onClicked: holder.openFile() }
            VButton { text: qsTr("测试画面"); onClicked: app.startTestPattern(false) }
        }
    }
}
