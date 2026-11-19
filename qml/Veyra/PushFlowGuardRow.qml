// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFlow's ghost guard (include/veyra/pushflow/PushFlowMotion.h), in Professional mode. Experimental
// and off unless a level is chosen: where the network could not follow the motion and shows two
// pictures at once, the generated frame shows one of them, the real frame nearer in time. Preference
// pushFlowGuard (0 off, 1 careful, 2 standard, 3 strong), live: PushFlow reads it at every generated
// frame. While it runs, the row says how much of the recent generated frames it changed.
import QtQuick
import QtQuick.Layouts
VRow {
    id: row
    objectName: "list-pushflow-guard-row"
    readonly property int level: Math.max(0, Math.min(3, Math.round(Number(veyra.preferences.pushFlowGuard || 0))))
    // The share of the recent generated frames given to one sample; below 0: not known (nothing plays,
    // PushFlow is not the method in use, or the guard has not started).
    property real share: -1
    label: qsTr("PushFlow 重影保护（实验性）")
    help: qsTr("动作太快、网络跟不上时，它会把前后两帧叠在一起，出现半透明的重影。开启后，这些地方只显示时间上更近的那一帧：没有重影，但这一小块按游戏原本的帧率更新。它只在两帧之间选择，不会凭空生成画面。实验功能：还没有在真实显卡和游戏上验证过效果")
    hint: level === 0 ? qsTr("关闭：PushFlow 的画面与以前完全相同")
        : share >= 0 ? qsTr("正在运行：最近的生成帧里有 %1% 的画面改为只显示一帧").arg((share * 100).toFixed(share < 0.1 ? 1 : 0))
        : qsTr("档位越高，越早把重影换成单帧。看到画面局部卡顿就调低")
    hintBelow: true
    function readShare() {
        const st = veyra.pushLiveStats() || ({})
        row.share = st.running === true && Number(st.pushFlowGuard || 0) > 0 ? Math.max(0, Math.min(1, Number(st.pushFlowGuardShare || 0))) : -1
    }
    RowLayout {
        spacing: 8
        VSelect {
            objectName: "list-pushflow-guard"
            options: [{id:"0",label:qsTr("关")},{id:"1",label:qsTr("谨慎")},{id:"2",label:qsTr("标准")},{id:"3",label:qsTr("强")}]
            value: options[row.level].label
            onPicked: id => veyra.setPreference("pushFlowGuard", Number(id))
        }
        Timer {
            objectName: "list-pushflow-guard-timer"
            interval: 1000; repeat: true; triggeredOnStart: true
            running: row.visible && row.level > 0 && veyra.hasSource === true
            onTriggered: row.readShare()
            onRunningChanged: if (!running) row.share = -1
        }
    }
}
