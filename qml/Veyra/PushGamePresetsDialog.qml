// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Game presets: ready-made settings per game (starting with GTA VI) whose HUD
// areas become "Exclude all effects" regions, so the minimap and HUD keep the
// console's own pixels and are never interpolated. Players save their own (with
// the areas they drew in Professional mode) and share them as a code or a
// .pushfps file. Stored in the pushGamePresets preference (validated by the bridge).
import QtQuick
import QtQuick.Layouts
import "PushGamePresets.js" as Presets
import "EffectSupport.js" as Support

DialogLayer {
    id: games
    objectName: "pushfps-game-presets-dialog"
    key: "games"
    glyph: "gamepad"
    title: qsTr("游戏预设")
    sub: qsTr("每个游戏的推荐设置，并让小地图和 HUD 保持清晰（这些区域不补帧、不做画质效果）")
    dialogWidth: 640

    readonly property var mine: {
        try {
            const list = JSON.parse(veyra.preferences.pushGamePresets || "[]")
            return Array.isArray(list) ? list.map(Presets.sanitize).filter(p => p !== null) : []
        } catch (e) { return [] }
    }
    readonly property var all: Presets.builtIn.map(Presets.sanitize).concat(games.mine.map(p => Object.assign({}, p, { mine: true })))
    property string selectedName: "GTA VI"
    readonly property var selected: games.all.find(p => p.name === games.selectedName) || games.all[0]
    property bool protectHud: true
    property string importText: ""
    property string message: ""
    property bool messageError: false

    // Built-in names and notes in the interface language (the library keeps English ids).
    function labelFor(p) {
        if (!p) return ""
        if (p.mine) return p.name + " · " + qsTr("我的")
        switch (p.id) {
        case "open-world-bl": return qsTr("开放世界 · 小地图在左下")
        case "open-world-tr": return qsTr("开放世界 · 小地图在右上")
        case "action-corners": return qsTr("动作 · HUD 在下方两角")
        case "racing": return qsTr("赛车 · 速度表在右下")
        case "shooter": return qsTr("射击 · HUD 在四个角")
        case "cinematic": return qsTr("电影感 · 不保护 HUD")
        }
        return p.name
    }
    function noteFor(p) {
        if (!p) return ""
        switch (p.mine ? "" : p.id) {
        case "gta6": return qsTr("小地图在左下，金钱和通缉等级在右上。位置按 GTA 系列推测，开始玩后请检查一下")
        case "open-world-bl": return qsTr("左下角的圆形或方形小地图")
        case "open-world-tr": return qsTr("右上角的小地图或指南针")
        case "action-corners": return qsTr("左下角血量，右下角武器和弹药")
        case "racing": return qsTr("右下角的速度、挡位和小地图")
        case "shooter": return qsTr("每个角都有小元素")
        case "cinematic": return qsTr("剧情游戏和电影：整个画面都补帧")
        }
        return p.note
    }
    function say(text, error) { games.message = text; games.messageError = error === true }
    function writeMine(list) { return veyra.setPreference("pushGamePresets", JSON.stringify(list.slice(0, 100))) }

    // --- apply ---------------------------------------------------------------
    function targetFps(id, refresh) { return id === "120" ? 120 : id === "60" ? 60 : id === "monitor" ? refresh : 0 }
    function multiplierFor(fps, gameFps) {
        const offered = (veyra.fgMultiplierChoices || []).map(o => parseInt(o.id)).filter(m => m >= 2).sort((a, b) => a - b)
        let pick = 0
        for (const m of offered) if (gameFps * m <= fps + 0.5) pick = m
        return pick
    }
    function apply(p) {
        if (!p) return
        veyra.setPreference("pushGameFps", p.fps)
        const refresh = veyra.displayRefreshHz > 0 ? veyra.displayRefreshHz : 60
        let target = Presets.resolveTarget(p.target, refresh)
        if (Support.available(veyra, "frame-generation")) {
            if (target === "off") veyra.fgEnabled = false
            else {
                let fps = games.targetFps(target, refresh)
                let m = games.multiplierFor(fps, p.fps)
                // "auto" falls back to 60 when this frame generation cannot reach 120.
                if (p.target === "auto" && target === "120" && p.fps * m + 0.5 < 120) { target = "60"; fps = 60; m = games.multiplierFor(60, p.fps) }
                if (fps <= p.fps + 0.5 || m < 2) veyra.fgEnabled = false
                else {
                    veyra.fgMultiplier = m
                    if (!veyra.presentationOwned) {
                        if (target === "monitor") veyra.outputRateMode = 1
                        else { veyra.outputCustomFps = fps; veyra.outputRateMode = 2 }
                    }
                }
            }
        }
        const regions = games.protectHud ? p.regions : []
        if (!veyra.pushSetExclusionRegions(regions, p.feather)) { games.say(qsTr("HUD 保护区域没有应用"), true); return }
        games.say(regions.length > 0 ? qsTr("已应用「%1」，%2 个 HUD 区域受保护。可在专业模式 → 画质 → 全效果剔除 中微调").arg(games.labelFor(p)).arg(regions.length)
                                     : qsTr("已应用「%1」").arg(games.labelFor(p)), false)
        veyra.logUi("push-presets", "apply " + p.name + " regions=" + regions.length + " target=" + target)
    }

    // --- your own ------------------------------------------------------------
    function currentAsPreset(name) {
        const ex = veyra.exclusionState || {}
        const regions = ex.enabled ? (ex.regions || []).map(r => ({ left: r.left, top: r.top, right: r.right, bottom: r.bottom, ellipse: r.ellipse === true })) : []
        const target = !veyra.fgEnabled ? "off" : veyra.outputRateMode === 1 ? "monitor"
                     : Math.abs(veyra.outputCustomFps - 120) < 0.5 ? "120" : Math.abs(veyra.outputCustomFps - 60) < 0.5 ? "60" : "auto"
        return Presets.sanitize({ name: name, fps: Number(veyra.preferences.pushGameFps || 30), target: target,
                                  feather: ex.feather !== undefined ? ex.feather : 6, regions: regions,
                                  note: qsTr("我的预设") })
    }
    function saveMine(name) {
        const p = games.currentAsPreset(name)
        if (!p) { games.say(qsTr("请先输入名称"), true); return false }
        if (Presets.builtIn.some(b => b.name === p.name)) { games.say(qsTr("这个名称已被内置预设使用"), true); return false }
        const list = games.mine.filter(x => x.name !== p.name)
        list.push(p)
        list.sort((a, b) => a.name.localeCompare(b.name))
        if (!games.writeMine(list)) { games.say(qsTr("保存失败"), true); return false }
        games.selectedName = p.name
        games.say(qsTr("已保存「%1」（%2 个 HUD 区域）").arg(p.name).arg(p.regions.length), false)
        return true
    }
    function addImported(p) {
        if (!p) { games.say(qsTr("这不是有效的 PushFPS 预设代码"), true); return false }
        let name = p.name
        if (Presets.builtIn.some(b => b.name === name)) name = (name + " " + qsTr("（分享）")).slice(0, Presets.kMaxName)
        p.name = name
        const list = games.mine.filter(x => x.name !== name)
        list.push(p)
        list.sort((a, b) => a.name.localeCompare(b.name))
        if (!games.writeMine(list)) { games.say(qsTr("保存失败"), true); return false }
        games.selectedName = name
        games.say(qsTr("已添加「%1」").arg(name), false)
        return true
    }
    function deleteMine(name) {
        games.writeMine(games.mine.filter(x => x.name !== name))
        games.selectedName = "GTA VI"
        games.say(qsTr("已删除「%1」").arg(name), false)
    }
    function shareCode(p) { return Presets.encode(p, Qt.btoa) }

    onShownChanged: if (shown) { games.message = ""; games.importText = "" }
    actions: [{ label: qsTr("关闭") }, { label: qsTr("应用"), primary: true }]
    onActionTriggered: label => {
        if (label === qsTr("应用")) games.apply(games.selected)
        else games.dialogHost.close()
    }

    DialogGroup {
        VRow {
            label: qsTr("游戏")
            VSelect {
                objectName: "pushfps-game-preset-select"
                implicitWidth: 300
                options: games.all.map(p => ({ id: p.name, label: games.labelFor(p) }))
                value: games.labelFor(games.selected)
                onPicked: id => games.selectedName = id
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 14; Layout.rightMargin: 14
            visible: games.noteFor(games.selected).length > 0
            text: games.noteFor(games.selected)
            color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 12; wrapMode: Text.WordWrap
        }
        // A small picture of where the protected areas are.
        Rectangle {
            objectName: "pushfps-game-preset-map"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8; Layout.bottomMargin: 8
            implicitWidth: 320; implicitHeight: 180
            radius: 8
            color: Theme.ink(0.04)
            border.width: 1; border.color: Theme.stroke
            Repeater {
                model: games.selected ? games.selected.regions : []
                delegate: Rectangle {
                    required property var modelData
                    x: modelData.left * 320; y: modelData.top * 180
                    width: (modelData.right - modelData.left) * 320; height: (modelData.bottom - modelData.top) * 180
                    radius: modelData.ellipse ? Math.min(width, height) / 2 : 3
                    color: Qt.rgba(0.537, 0.961, 0.165, games.protectHud ? 0.22 : 0.06)
                    border.width: 1; border.color: Theme.accent
                }
            }
            Text {
                anchors.centerIn: parent
                visible: !games.selected || games.selected.regions.length === 0
                text: qsTr("整个画面补帧")
                color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 12
            }
        }
        VRow {
            label: qsTr("保护 HUD")
            hint: qsTr("绿色区域保持主机原画，不补帧，文字和小地图不会糊")
            VSwitch { objectName: "pushfps-game-preset-hud"; checked: games.protectHud; onToggled: checked => games.protectHud = checked }
        }
        VRow {
            label: qsTr("HUD 位置不对？")
            hint: qsTr("在你的游戏画面上自动识别 HUD，或手动拖动方框")
            VButton {
                objectName: "pushfps-game-preset-edit-hud"
                ghost: true
                iconName: "scan"
                text: qsTr("调整 HUD 区域")
                onClicked: {
                    if (veyra.hasSource) veyra.navigate("pro")
                    veyra.navigate("hud")
                }
            }
        }
        VRow {
            label: qsTr("游戏帧率 · 目标")
            value: games.selected ? games.selected.fps + " → " + (games.selected.target === "auto" ? qsTr("自动") : games.selected.target === "off" ? qsTr("不补帧")
                                    : games.selected.target === "monitor" ? qsTr("跟随显示器") : games.selected.target + " FPS") : ""
        }
    }

    DialogSection { text: qsTr("分享") }
    DialogGroup {
        VRow {
            label: qsTr("分享这个预设")
            hint: qsTr("复制代码发给朋友或贴到 Reddit / Discord；也可以保存为文件")
            RowLayout {
                spacing: 6
                VButton {
                    objectName: "pushfps-game-preset-copy"
                    text: qsTr("复制代码"); ghost: true; iconName: "copy"
                    onClicked: { if (veyra.pushCopyText(games.shareCode(games.selected))) games.say(qsTr("代码已复制"), false) }
                }
                VButton {
                    text: qsTr("保存文件"); ghost: true
                    onClicked: veyra.pushExportTextFile(qsTr("保存游戏预设"), games.selected.name, games.shareCode(games.selected))
                }
            }
        }
        VRow {
            label: qsTr("添加别人的预设")
            // A VRow's slot does not lay out several controls: they go in a row here.
            RowLayout {
                spacing: 6
                DialogTextField {
                    objectName: "pushfps-game-preset-code"
                    implicitWidth: 200
                    placeholder: qsTr("粘贴 PUSHFPS1: 代码")
                    onTyped: t => games.importText = t
                    onEdited: t => games.importText = t
                }
                VButton {
                    objectName: "pushfps-game-preset-add"
                    text: qsTr("添加"); ghost: true
                    onClicked: games.addImported(Presets.decode(games.importText, Qt.atob))
                }
                VButton {
                    objectName: "pushfps-game-preset-open"
                    text: qsTr("打开文件"); ghost: true
                    onClicked: { const t = veyra.pushImportTextFile(qsTr("打开游戏预设")); if (t.length > 0) games.addImported(Presets.decode(t, Qt.atob)) }
                }
            }
        }
        VRow {
            label: qsTr("保存当前设置为预设")
            hint: qsTr("包含游戏帧率、目标帧率和你在专业模式中画的全效果剔除区域")
            RowLayout {
                spacing: 6
                DialogTextField {
                    id: ownName
                    objectName: "pushfps-game-preset-name"
                    implicitWidth: 180
                    placeholder: qsTr("名称")
                }
                VButton { objectName: "pushfps-game-preset-save"; text: qsTr("保存"); ghost: true; onClicked: games.saveMine(ownName.text) }
            }
        }
        VRow {
            visible: !!games.selected && games.selected.mine === true
            label: qsTr("删除「%1」").arg(games.selected ? games.selected.name : "")
            VButton { text: qsTr("删除"); ghost: true; iconName: "trash"; onClicked: games.deleteMine(games.selected.name) }
        }
    }
    Text {
        objectName: "pushfps-game-preset-message"
        Layout.fillWidth: true
        Layout.topMargin: 8
        visible: games.message.length > 0
        text: games.message
        color: games.messageError ? Theme.warn : Theme.accent
        font.family: Theme.fontUi; font.pixelSize: 12; wrapMode: Text.WordWrap
    }
}
