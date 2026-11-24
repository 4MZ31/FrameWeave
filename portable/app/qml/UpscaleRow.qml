// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// "Upscale to my screen": the switch of the super resolution stage, as the Windows build has it in quick
// setup's Picture section (home page) and at the top of Professional mode's Quality tab. Its hint
// says what upscaling does now (FrameView::UpscaleState, handed to `app` by Main.qml). The algorithm, the
// target size and the sharpening are in the Super resolution card (SuperResolutionCard.qml).
import QtQuick
import QtQuick.Window

VRow {
    id: row
    objectName: "pushfps-upscale-row"

    readonly property int screenHigh: Math.round(Screen.height * Screen.devicePixelRatio)
    readonly property int pictureHigh: app.running ? Number(app.stats.height || 0) : 0
    // As the Windows build's PushUpscale.js `gains`: a picture at least 1.2 times lower than the screen.
    readonly property bool gains: pictureHigh >= 360 && pictureHigh * 1.2 < screenHigh

    label: qsTr("放大到屏幕")
    help: qsTr("全屏时画面只是被拉大到屏幕大小，所以看起来发软。开启放大后由显卡把画面放大到屏幕分辨率，更清晰，也更吃显卡；帧率掉了就关掉")
    hint: {
        if (!app.srEnabled) {
            if (row.gains) return qsTr("画面是 %1p，屏幕是 %2p：开启后全屏更清晰").arg(row.pictureHigh).arg(row.screenHigh)
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
    // As the Windows build's setUpscale (PushQuickSetup.qml) with PushUpscale.js targetIndex: switched on,
    // the size follows the screen, 2K for one up to 1440 lines and 4K above, but only between those two:
    // a 5K or 6K chosen in the Super resolution card is left alone.
    function targetFor(screenHigh, now) {
        if (!(screenHigh > 0) || (now !== 1 && now !== 2)) return 0
        const want = screenHigh <= 1440 ? 1 : 2
        return want === now ? 0 : want
    }
    function setUpscale(on) {
        if (on) {
            const size = row.targetFor(row.screenHigh, app.srTargetIndex)
            if (size > 0) app.srTargetIndex = size
        }
        app.srEnabled = on
    }
    VSwitch { objectName: "pushfps-upscale"; checked: app.srEnabled; onToggled: on => row.setUpscale(on) }
}
