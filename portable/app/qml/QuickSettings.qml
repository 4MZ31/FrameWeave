// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Frame generation and what it waits for: the rows of the quick setup's first sections. Used by the home
// page's settings pane and by Professional mode's Frame generation block, so each setting is written once.
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    spacing: 0
    Layout.fillWidth: true
    readonly property bool generating: app.targetMode !== 0

    VRow {
        objectName: "home-target-row"
        label: qsTr("目标帧率")
        hint: app.targetMode === 0 ? qsTr("补帧关闭：每一帧到达后立刻显示，延迟最低")
                                   : qsTr("补出来的帧夹在两张真实画面之间，画面会晚一张真实画面显示")
        hintBelow: true
        VSeg {
            objectName: "home-target"
            options: [{ id: "0", label: qsTr("关") }, { id: "1", label: "60 FPS" }, { id: "2", label: "120 FPS" }, { id: "3", label: qsTr("匹配显示器") }]
            current: String(app.targetMode)
            onPicked: id => app.targetMode = Number(id)
        }
    }
    VRow {
        label: qsTr("最低延迟")
        hint: app.lowLatency ? qsTr("已开启：运动估计用「快速」，帧节奏修复关闭。关闭后恢复原来的设置")
                             : qsTr("一键减少等待：补帧照常，每对画面的运动更快算好，画面少等一会儿")
        enabled: parent.generating
        opacity: enabled ? 1 : 0.5
        VSwitch { objectName: "home-lowlatency"; checked: app.lowLatency; onToggled: on => app.lowLatency = on }
    }
    VRow {
        label: qsTr("帧节奏修复")
        hint: qsTr("把出帧不均匀的锁帧游戏重新对齐到均匀的时间，每帧最多晚一次刷新。需要开启自动去重")
        enabled: parent.generating
        opacity: enabled ? 1 : 0.5
        VSwitch { objectName: "home-pacing"; checked: app.pacingRepair; onToggled: on => app.pacingRepair = on }
    }
    VRow {
        label: qsTr("实时 HUD 锁定")
        hint: qsTr("每一帧自动找出 HUD，文字和准星在补出来的帧里保持清晰")
        enabled: parent.generating
        opacity: enabled ? 1 : 0.5
        VSwitch { objectName: "home-hud"; checked: app.hudLock; onToggled: on => app.hudLock = on }
    }
    VRow {
        label: qsTr("去除重复帧")
        hint: qsTr("30 FPS 的游戏在 60 Hz 信号里每张画面会发两次；补帧只看真正的新画面")
        VSwitch { objectName: "home-dedup"; checked: app.removeDuplicates; onToggled: on => app.removeDuplicates = on }
    }
    VRow {
        label: qsTr("运动估计")
        hint: qsTr("精细：按 16 像素找运动，更准；快速：按 32 像素，占用更少")
        VSeg {
            objectName: "home-quality"
            options: [{ id: "0", label: qsTr("快速") }, { id: "1", label: qsTr("精细") }]
            current: String(app.quality)
            onPicked: id => app.quality = Number(id)
        }
    }
    VRow {
        label: qsTr("在画面上显示延迟")
        hint: qsTr("游戏帧率、输出帧率和延迟（按 Tab 切换）")
        VSwitch { objectName: "home-stats"; checked: app.showStats; onToggled: on => app.showStats = on }
    }
}
