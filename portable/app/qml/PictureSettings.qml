// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// How the picture is drawn: its shape in the window, upscaling's switch, and the four picture controls.
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: picture
    spacing: 0
    Layout.fillWidth: true

    // "Upscale to my screen" (UpscaleRow.qml), as in the Windows build's quick setup. Professional mode
    // has it in its Quality tab instead, with the Super resolution card.
    property bool showUpscale: true
    // The picture's shape in the window. Professional mode has it in its Display tab, as on Windows.
    property bool showShape: true

    component Control: VRow {
        id: row
        property real from: 0
        property real to: 1
        property real neutral: 0
        property real current: 0
        signal changed(real v)
        value: current.toFixed(2)
        VSlider {
            implicitWidth: 150
            from: row.from; to: row.to
            center: Math.abs((row.neutral - row.from) - (row.to - row.neutral)) < 1e-6
            value: row.current
            onMoved: v => row.changed(v)
        }
    }

    VRow {
        visible: picture.showShape
        label: qsTr("画面比例")
        hint: qsTr("窗口和画面形状不同时：保留黑边、裁掉两侧铺满，或拉伸铺满")
        hintBelow: true
        VSeg {
            objectName: "home-fill"
            options: [{ id: "0", label: qsTr("黑边") }, { id: "1", label: qsTr("裁切铺满") }, { id: "2", label: qsTr("拉伸") }]
            current: String(app.fillMode)
            onPicked: id => app.fillMode = Number(id)
        }
    }
    UpscaleRow { visible: picture.showUpscale }
    Control { objectName: "picture-brightness"; label: qsTr("亮度"); from: -0.5; to: 0.5; neutral: 0; current: app.brightness; onChanged: v => app.brightness = v }
    Control { objectName: "picture-contrast"; label: qsTr("对比度"); from: 0.5; to: 1.5; neutral: 1; current: app.contrast; onChanged: v => app.contrast = v }
    Control { objectName: "picture-saturation"; label: qsTr("饱和度"); from: 0; to: 2; neutral: 1; current: app.saturation; onChanged: v => app.saturation = v }
    Control { objectName: "picture-gamma"; label: qsTr("伽马"); from: 0.5; to: 2; neutral: 1; current: app.gamma; onChanged: v => app.gamma = v }
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 6
        Item { Layout.fillWidth: true }
        VButton { objectName: "picture-reset"; text: qsTr("重置"); iconName: "reset"; onClicked: app.resetPicture() }
    }
}
