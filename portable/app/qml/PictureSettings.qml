// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// How the picture is drawn: its shape in the window, upscaling, and the four picture controls.
import QtQuick
import QtQuick.Layouts
import QtQuick.Window

ColumnLayout {
    id: picture
    spacing: 0
    Layout.fillWidth: true

    // Upscaling (FrameView, NisNode.h): what it says depends on what the picture reports.
    readonly property int screenHigh: Math.round(Screen.height * Screen.devicePixelRatio)
    readonly property int pictureHigh: app.running ? Number(app.stats.height || 0) : 0
    // As the Windows build's PushUpscale.js `gains`: a picture at least 1.2 times lower than the screen.
    readonly property bool upscaleGains: picture.pictureHigh >= 360 && picture.pictureHigh * 1.2 < picture.screenHigh
    readonly property int sharpness: (veyra.preferences || {}).nisSharpness === undefined ? 50 : Number(veyra.preferences.nisSharpness)
    readonly property string upscaleHint: {
        if (!app.srEnabled) {
            if (picture.upscaleGains) return qsTr("画面是 %1p，屏幕是 %2p：开启后全屏更清晰").arg(picture.pictureHigh).arg(picture.screenHigh)
            return qsTr("放大到屏幕分辨率")
        }
        switch (app.upscaleState) {
        case 1: return qsTr("正在使用 %1").arg("NVIDIA Image Scaling · " + app.upscaleSizes)
        case 2: return qsTr("画面显示得不比原尺寸大，现在不需要放大")
        case 3: return qsTr("这台电脑的图形接口不能运行放大：需要 Vulkan、Metal 或 OpenGL 4.3")
        case 4: return qsTr("放大没能在这块显卡上启动，画面照常显示")
        }
        return qsTr("有画面播放时开始放大")
    }

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
    VRow {
        objectName: "pushfps-upscale-row"
        label: qsTr("放大到屏幕")
        help: qsTr("全屏时画面只是被拉大到屏幕大小，所以看起来发软。开启放大后由显卡把画面放大到屏幕分辨率，更清晰，也更吃显卡；帧率掉了就关掉")
        hint: picture.upscaleHint
        VSwitch { objectName: "pushfps-upscale"; checked: app.srEnabled; onToggled: on => app.srEnabled = on }
    }
    VRow {
        objectName: "picture-sr-target-row"
        visible: app.srEnabled
        label: qsTr("目标尺寸")
        VSeg {
            objectName: "picture-sr-target"
            options: [{ id: "1", label: "2K" }, { id: "2", label: "4K" }]
            current: String(app.srTargetIndex)
            onPicked: id => app.srTargetIndex = Number(id)
        }
    }
    VRow {
        objectName: "picture-nis-sharpness-row"
        visible: app.srEnabled
        label: qsTr("NIS 锐化")
        help: qsTr("NVIDIA Image Scaling 是一个开源的放大和锐化滤镜，任何品牌的显卡都能用。锐化太高会在边缘出现白边")
        hint: qsTr("0 只放大不锐化；50 为 NVIDIA 默认。一次最多放大 2 倍，更大的倍数由普通缩放补足。")
        value: picture.sharpness + "%"
        VSlider {
            objectName: "picture-nis-sharpness"
            implicitWidth: 150
            from: 0; to: 100; stepSize: 1
            valueFromModel: true
            value: picture.sharpness
            resettable: true; defaultValue: 50
            live: false
            onMoved: v => veyra.setPreference("nisSharpness", Math.round(Math.max(0, Math.min(100, v))))
        }
    }
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
