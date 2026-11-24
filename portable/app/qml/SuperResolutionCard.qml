// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The Super resolution card of Professional mode's Quality tab, as the Windows build has it (ProPage.qml
// `list-sr`): on or off, the upscaling algorithm, the target size and, for NVIDIA Image Scaling, its
// sharpening (the rows of the Windows SrStageEditor.qml that apply here, under the same names). The
// switch is the same setting as "Upscale to my screen" (UpscaleRow.qml). The preview has one algorithm,
// NVIDIA Image Scaling (the Windows build's choice 6, `videoSrQuality`).
import QtQuick
import QtQuick.Layouts

VAccordion {
    id: card
    objectName: "list-sr"
    Layout.fillWidth: true
    glyph: "sparkles"
    hue: "#4F7BFF"
    title: qsTr("超分辨率")

    readonly property var algorithms: [{ id: "6", label: "NVIDIA Image Scaling" }]
    // The Windows build's ids and order; 7K and 8K are left out (no capture card sends more than 4K).
    readonly property var targets: [{ id: "1", label: "2K" }, { id: "2", label: "4K" }, { id: "4", label: "5K" }, { id: "5", label: "6K" }]
    readonly property string targetLabel: (targets.find(o => o.id === String(app.srTargetIndex)) || targets[1]).label
    readonly property string algorithmLabel: (algorithms.find(o => o.id === String(app.videoSrQuality)) || algorithms[0]).label
    readonly property bool nis: app.videoSrQuality === 6
    readonly property int sharpness: (veyra.preferences || {}).nisSharpness === undefined ? 50 : Number(veyra.preferences.nisSharpness)

    // As on Windows: the algorithm (or "off"), then the target size.
    summary: (app.srEnabled ? card.algorithmLabel : qsTr("已关闭")) + " · " + card.targetLabel
    on: app.srEnabled
    open: true
    onToggled: on => app.srEnabled = on

    VRow {
        label: qsTr("超分算法")
        VSelect {
            objectName: "list-sr-backend"
            implicitWidth: 210
            options: card.algorithms
            value: card.algorithmLabel
            onPicked: id => app.videoSrQuality = Number(id)
        }
    }
    VRow {
        label: qsTr("目标尺寸")
        VSeg {
            objectName: "list-sr-target"
            options: card.targets
            current: String(app.srTargetIndex)
            onPicked: id => app.srTargetIndex = Number(id)
        }
    }
    VRow {
        objectName: "list-nis-sharpness-row"
        visible: card.nis
        label: qsTr("NIS 锐化")
        help: qsTr("NVIDIA Image Scaling 是一个开源的放大和锐化滤镜，任何品牌的显卡都能用。锐化太高会在边缘出现白边")
        hint: qsTr("0 只放大不锐化；50 为 NVIDIA 默认。一次最多放大 2 倍，更大的倍数由普通缩放补足。")
        value: card.sharpness + "%"
        VSlider {
            objectName: "list-nis-sharpness"
            implicitWidth: 150
            from: 0; to: 100; stepSize: 1
            valueFromModel: true
            value: card.sharpness
            resettable: true; defaultValue: 50
            live: false
            onMoved: v => veyra.setPreference("nisSharpness", Math.round(Math.max(0, Math.min(100, v))))
        }
    }
}
