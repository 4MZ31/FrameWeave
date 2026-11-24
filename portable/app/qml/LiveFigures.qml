// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// What the engine reports about the running picture, in three cards under the picture of Professional
// mode, as the Windows build has its meters there: the signal, the motion work (on the processor here,
// where Windows times its graphics stages) and the frame rate and pacing. A figure that has no reading
// yet is a dash, never a zero. Like Windows, a narrow row leaves out the first card.
import QtQuick
import QtQuick.Layouts

RowLayout {
    id: figures
    spacing: 10
    readonly property var st: app.stats
    readonly property bool live: app.running && Number(st.framesRead || 0) > 0
    readonly property string dash: "—"
    function fps(v) { return live && Number(v) > 0 ? Math.round(Number(v)) + " FPS" : dash }
    function ms(v, digits) { return live && Number(v) >= 0 && v !== undefined ? Number(v).toFixed(digits || 0) + " ms" : dash }
    function count(v) { return live && v !== undefined ? String(v) : dash }

    // One card: a title and its figures. (An inline component does not see this file's ids.)
    component Card: Rectangle {
        id: card
        property string title: ""
        default property alias rows: list.data
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumWidth: 192
        Layout.preferredWidth: 210
        radius: Theme.rCard
        color: Theme.card
        border.width: 1
        border.color: Theme.stroke
        ColumnLayout {
            id: list
            anchors.fill: parent
            anchors.margins: 14
            spacing: 6
            VEyebrow { Layout.fillWidth: true; text: card.title }
        }
    }
    // One figure: its name on the left, its reading on the right.
    component Figure: RowLayout {
        id: fig
        property string label: ""
        property string reading: ""
        Layout.fillWidth: true
        spacing: 8
        Text {
            Layout.fillWidth: true
            text: fig.label
            color: Theme.t2
            elide: Text.ElideRight
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
        }
        Text {
            text: fig.reading
            color: fig.reading === "—" ? Theme.t3 : Theme.t1
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
            font.features: ({ "tnum": 1 })
        }
    }

    Card {
        objectName: "pro-meter-signal"
        visible: figures.width >= 850
        title: qsTr("信号")
        Figure { objectName: "figure-size"; label: qsTr("画面尺寸"); reading: figures.live && Number(figures.st.width) > 0 ? figures.st.width + " × " + figures.st.height : figures.dash }
        Figure { objectName: "figure-signal"; label: qsTr("信号"); reading: figures.fps(figures.st.transportFps) }
        Figure { objectName: "figure-game"; label: qsTr("游戏帧率"); reading: figures.fps(figures.st.contentFps) }
        Figure { objectName: "figure-sound"; label: qsTr("声音延迟"); reading: figures.ms(figures.st.audioDelayMs) }
        Item { Layout.fillHeight: true }
    }
    Card {
        objectName: "pro-meter-motion"
        title: qsTr("运动估计")
        Figure { objectName: "figure-estimate"; label: qsTr("运动估计"); reading: figures.ms(figures.st.estimateMs, 1) }
        Figure { objectName: "figure-margin"; label: qsTr("这台电脑的额外等待"); reading: figures.ms(figures.st.marginMs) }
        Figure { objectName: "figure-late"; label: qsTr("没赶上的运动"); reading: figures.count(figures.st.lateFields) }
        Figure { objectName: "figure-cuts"; label: qsTr("镜头切换"); reading: figures.count(figures.st.sceneCuts) }
        Figure { objectName: "figure-duplicates"; label: qsTr("去除的重复帧"); reading: figures.count(figures.st.duplicates) }
        Item { Layout.fillHeight: true }
    }
    Card {
        objectName: "pro-meter-rate"
        title: qsTr("帧率与节奏")
        Figure { objectName: "figure-output"; label: qsTr("输出帧率"); reading: figures.fps(figures.st.outputFps) }
        Figure { objectName: "figure-latency"; label: qsTr("延迟"); reading: figures.ms(figures.st.latencyMs) }
        Figure {
            objectName: "figure-pacing"
            visible: app.pacingRepair
            label: qsTr("帧节奏修复")
            reading: figures.live ? Math.round(Number(figures.st.pacingShare || 0) * 100) + " %" : figures.dash
        }
        Item { Layout.fillHeight: true }
    }
}
