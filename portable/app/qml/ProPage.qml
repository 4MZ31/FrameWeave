// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Professional mode: the picture large on the left, every setting of the preview in the panel on the
// right (Quality, Frame Gen, Picture, Audio, Statistics). The panel starts on All, where each block stands under a title that folds it (preference
// pushProFolded, as in the Windows build); the tabs show one block alone.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "page-pro"
    color: Theme.bgMid
    property bool sideCollapsed: false
    signal requestFullscreen()
    signal requestScreenshot()
    signal requestSources()

    readonly property Item videoViewport: viewport
    readonly property real margin: 16
    readonly property real gap: 14
    readonly property real panelWidth: Math.round(Math.max(340, Math.min(420, width * 0.32)))
    readonly property real actionHeight: 44

    // The blocks, in the order All shows them.
    readonly property var blocks: [
        { id: "quality", label: qsTr("画质") },
        { id: "fg", label: qsTr("补帧") },
        { id: "color", label: qsTr("画面") },
        { id: "audio", label: qsTr("声音") },
        { id: "stats", label: qsTr("统计") }
    ]
    property string tab: "all"
    readonly property var folded: String((veyra.preferences || {}).pushProFolded || "").split(",").filter(s => s.length > 0)
    function blockShown(id) { return tab === id || (tab === "all" && folded.indexOf(id) < 0) }
    function setFolded(id, fold) {
        const next = folded.filter(s => s !== id)
        if (fold) next.push(id)
        veyra.setPreference("pushProFolded", next.join(","))
    }

    // A block of the panel: under a title that folds it in All, alone under its tab. (An inline component
    // does not see this file's ids: the tab and whether the block is shown are handed in.)
    component Block: ColumnLayout {
        id: block
        property string blockId: ""
        property string title: ""
        property string tab: "all"
        property bool shown: true
        signal foldToggled(bool open)
        default property alias rows: body.data
        Layout.fillWidth: true
        spacing: 0
        visible: tab === "all" || tab === blockId
        PushFoldHead {
            objectName: "pro-all-" + block.blockId
            visible: block.tab === "all"
            text: block.title
            open: block.shown
            onToggled: open => block.foldToggled(open)
        }
        ColumnLayout {
            id: body
            objectName: "pro-block-" + block.blockId
            visible: block.shown
            Layout.fillWidth: true
            Layout.leftMargin: block.tab === "all" ? 4 : 0
            spacing: 0
        }
    }

    // ---- the picture ----
    readonly property real videoWidth: Math.max(1, width - 2 * margin - (sideCollapsed ? 0 : panelWidth + gap))
    Rectangle {
        id: videoCard
        objectName: "pro-video"
        x: root.margin; y: root.margin
        width: root.videoWidth
        height: Math.max(80, parent.height - 2 * root.margin - root.actionHeight - root.gap)
        radius: 4
        color: "black"
        border.width: 1
        border.color: Theme.stroke
        Item {
            id: viewport
            objectName: "pro-viewport"
            anchors.fill: parent
            anchors.margins: 1
            clip: true
            PicturePlaceholder {
                anchors.fill: parent
                z: 1
                offerSources: false
            }
        }
    }
    RowLayout {
        objectName: "pro-actions"
        x: root.margin
        y: videoCard.y + videoCard.height + root.gap
        width: root.videoWidth
        height: root.actionHeight
        spacing: 8
        VDot { off: !app.running }
        Text {
            Layout.fillWidth: true
            text: app.running ? (app.sourceTitle.length > 0 ? app.sourceTitle : qsTr("已连接")) : qsTr("未连接")
            color: app.running ? Theme.t1 : Theme.t3
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
            elide: Text.ElideMiddle
        }
        VButton {
            objectName: "pro-sources"
            visible: !app.running
            text: qsTr("片源")
            iconName: "video"
            onClicked: root.requestSources()
        }
        VButton {
            objectName: "pro-connect"
            visible: !app.running
            primary: true
            text: qsTr("连接")
            enabled: app.videoDevices.length > 0
            onClicked: app.startCamera()
        }
        VButton {
            objectName: "pro-screenshot"
            visible: app.running
            icon: true
            iconName: "camera"
            tip: qsTr("截图")
            onClicked: root.requestScreenshot()
        }
        VButton {
            objectName: "pro-stop"
            visible: app.running
            ghost: true
            text: qsTr("断开")
            onClicked: app.stop()
        }
        VButton {
            objectName: "pro-fullscreen"
            visible: app.running
            primary: true
            iconName: "max"
            text: qsTr("全屏")
            onClicked: root.requestFullscreen()
        }
    }

    // ---- the panel ----
    Rectangle {
        id: panel
        objectName: "pro-inspector"
        visible: !root.sideCollapsed
        x: root.width - root.margin - root.panelWidth
        y: root.margin
        width: root.panelWidth
        height: root.height - 2 * root.margin
        radius: Theme.rCard
        color: Theme.card2
        border.width: 1
        border.color: Theme.stroke

        Flickable {
            id: tabFlick
            x: 12; y: 12
            width: parent.width - 24
            height: tabs.implicitHeight
            contentWidth: tabs.implicitWidth
            interactive: contentWidth > width
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            VSeg {
                id: tabs
                objectName: "pro-tabs"
                options: [{ id: "all", label: qsTr("全部") }].concat(root.blocks)
                current: root.tab
                onPicked: id => root.tab = id
            }
        }
        Flickable {
            id: listFlick
            objectName: "pro-list"
            x: 14; y: tabFlick.y + tabFlick.height + 10
            width: parent.width - 18
            height: parent.height - y - 8
            contentHeight: list.implicitHeight + 8
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: VScrollBar {}
            ColumnLayout {
                id: list
                width: listFlick.width - 12
                spacing: 4

                // Quality, as the Windows build's tab: "Upscale to my screen" and the Super resolution card.
                Block {
                    blockId: "quality"; title: qsTr("画质"); tab: root.tab; shown: root.blockShown("quality"); onFoldToggled: open => root.setFolded("quality", !open)
                    UpscaleRow {}
                    SuperResolutionCard { Layout.topMargin: 6; Layout.bottomMargin: 6 }
                }
                Block { blockId: "fg"; title: qsTr("补帧"); tab: root.tab; shown: root.blockShown("fg"); onFoldToggled: open => root.setFolded("fg", !open); QuickSettings {} }
                Block { blockId: "color"; title: qsTr("画面"); tab: root.tab; shown: root.blockShown("color"); onFoldToggled: open => root.setFolded("color", !open); PictureSettings { showUpscale: false } }
                Block { blockId: "audio"; title: qsTr("声音"); tab: root.tab; shown: root.blockShown("audio"); onFoldToggled: open => root.setFolded("audio", !open); SoundSettings {} }
                Block { blockId: "stats"; title: qsTr("统计"); tab: root.tab; shown: root.blockShown("stats"); onFoldToggled: open => root.setFolded("stats", !open); LiveFigures {} }
            }
        }
    }
}
