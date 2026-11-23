// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The capture card's sound: played or not, how loud, moved by hand against the picture, and which devices.
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    spacing: 0
    Layout.fillWidth: true

    VRow {
        label: qsTr("播放采集卡声音")
        hint: qsTr("采集卡的声音与画面同步延迟后播放")
        VSwitch { objectName: "sound-on"; checked: app.audioPassthrough; onToggled: on => app.audioPassthrough = on }
    }
    VRow {
        label: qsTr("音量")
        value: Math.round(app.volume * 100) + " %"
        VSlider {
            objectName: "sound-volume"
            implicitWidth: 150
            from: 0; to: 1
            value: app.volume
            onMoved: v => app.volume = v
        }
    }
    VRow {
        label: qsTr("手动对口型")
        hint: app.audioTrimMs === 0 ? qsTr("声音自动跟着画面延迟。人声比嘴型早就调晚，比嘴型晚就调早")
                                    : qsTr("在自动延迟之上再调 %1 毫秒").arg((app.audioTrimMs > 0 ? "+" : "") + app.audioTrimMs)
        hintBelow: true
        RowLayout {
            spacing: 6
            VButton { objectName: "home-lipsync-earlier"; text: qsTr("提前"); enabled: app.audioTrimMs > -250; onClicked: app.audioTrimMs = app.audioTrimMs - 10 }
            Text {
                objectName: "home-lipsync-value"
                text: (app.audioTrimMs > 0 ? "+" : "") + app.audioTrimMs + " ms"
                color: Theme.t1
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsBody
                font.features: ({ "tnum": 1 })
                horizontalAlignment: Text.AlignHCenter
                Layout.preferredWidth: 60
            }
            VButton { objectName: "home-lipsync-later"; text: qsTr("延后"); enabled: app.audioTrimMs < 250; onClicked: app.audioTrimMs = app.audioTrimMs + 10 }
            VButton { visible: app.audioTrimMs !== 0; icon: true; iconName: "reset"; tip: qsTr("重置"); onClicked: app.audioTrimMs = 0 }
        }
    }
    VRow {
        label: qsTr("输入")
        VSelect {
            objectName: "sound-input"
            implicitWidth: 190
            value: app.audioInputs[app.audioInputIndex] || ""
            options: app.audioInputs.map((name, i) => ({ id: String(i), label: name }))
            onPicked: id => app.audioInputIndex = Number(id)
        }
    }
    VRow {
        label: qsTr("输出")
        VSelect {
            objectName: "sound-output"
            implicitWidth: 190
            value: app.audioOutputs[app.audioOutputIndex] || ""
            options: app.audioOutputs.map((name, i) => ({ id: String(i), label: name }))
            onPicked: id => app.audioOutputIndex = Number(id)
        }
    }
}
