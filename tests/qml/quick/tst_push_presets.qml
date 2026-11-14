// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// Game presets (PushGamePresetsDialog + PushGamePresets.js), the latency readout,
// the capture card status and Discord sharing, against the fixture bridge.
import QtQuick
import QtTest
import Veyra
import "../qml/Veyra/PushGamePresets.js" as Presets
Item {
    id: root
    width: 900; height: 1000
    Item { id: host; anchors.fill: parent; property string dialog: "games"; function close() { dialog = "" } }
    PushGamePresetsDialog { id: games; dialogHost: host }
    PushLatencyBadge { id: badge; x: 10; y: 10 }
    PushQuickSetup { id: quick; width: 560; y: 400 }
    PushRecordingSaved { id: card; x: 10; y: 900 }

    TestCase {
        name: "PushPresets"
        when: windowShown
        function init() {
            veyra.preferences = ({ pushGameFps: 30 })
            veyra.fgEnabled = false; veyra.fgMultiplier = 1; veyra.outputRateMode = 0; veyra.outputCustomFps = 0
            veyra.displayRefreshHz = 240
            veyra.exclusionState = ({ enabled: false, feather: 6, regions: [] })
            veyra.lastRegions = null; veyra.lastCopiedText = ""; veyra.lastCopiedFile = ""; veyra.discordOpened = 0; veyra.lastSharedFile = ""
            veyra.liveStats = ({ running: false })
            veyra.captureDeviceLabel = ""
            host.dialog = "games"
            games.protectHud = true
            games.selectedName = "GTA VI"
        }
        function cleanup() { init() }

        // --- presets ---------------------------------------------------------
        function test_library_is_valid() {
            verify(Presets.builtIn.length >= 5)
            for (const p of Presets.builtIn) {
                const s = Presets.sanitize(p)
                verify(s !== null, p.name)
                compare(s.regions.length, p.regions.length, p.name + " keeps every region")
                for (const r of s.regions) verify(r.left < r.right && r.top < r.bottom && r.right <= 1 && r.bottom <= 1, p.name)
            }
            compare(Presets.builtIn[0].name, "GTA VI")
        }
        function test_apply_gta_vi() {
            games.apply(games.selected)
            compare(veyra.lastRegions.length, 2, "minimap and money/wanted areas")
            compare(veyra.fgMultiplier, 4, "auto target on a 240 Hz screen: 120 FPS from 30")
            compare(veyra.outputCustomFps, 120)
            compare(veyra.preferences.pushGameFps, 30)
            verify(games.message.indexOf("GTA VI") >= 0)
            verify(!games.messageError)
        }
        function test_apply_without_hud_protection() {
            games.protectHud = false
            games.apply(games.selected)
            compare(veyra.lastRegions.length, 0)
        }
        function test_auto_target_on_a_60_hz_screen() {
            veyra.displayRefreshHz = 60
            games.apply(games.selected)
            compare(veyra.fgMultiplier, 2)
            compare(veyra.outputCustomFps, 60)
        }
        function test_share_code_round_trip() {
            const code = games.shareCode(games.selected)
            verify(code.indexOf("PUSHFPS1:") === 0)
            const back = Presets.decode(code, Qt.atob)
            compare(back.name, "GTA VI")
            compare(back.regions.length, 2)
            fuzzyCompare(back.regions[0].left, 0.012, 0.001)
            const unicode = Presets.decode(Presets.encode({ name: "原神 ★ test", fps: 60, regions: [] }, Qt.btoa), Qt.atob)
            compare(unicode.name, "原神 ★ test")
            compare(unicode.fps, 60)
            compare(Presets.decode("PUSHFPS1:not-base64!!", Qt.atob), null)
            compare(Presets.decode("hello", Qt.atob), null)
            compare(Presets.decode("", Qt.atob), null)
        }
        function test_bad_values_are_cleaned() {
            const p = Presets.sanitize({ name: "  x  ", fps: 999, target: "9000", feather: -5,
                                         regions: [{ l: -1, t: 2, r: 0.5, b: 0.5 }, { l: 0.1, t: 0.1, r: 0.1, b: 0.9 }, {}, {}, {}, {}] })
            compare(p.name, "x")
            compare(p.fps, 30)
            compare(p.target, "auto")
            compare(p.feather, 0)
            compare(p.regions.length, 1, "empty and zero-width regions dropped")
            compare(p.regions[0].left, 0); compare(p.regions[0].top, 0.5); compare(p.regions[0].bottom, 1)
            compare(Presets.sanitize({ name: "" }), null)
            const many = Presets.sanitize({ name: "m", regions: [0, 1, 2, 3, 4, 5].map(i => ({ l: i * 0.1, t: 0, r: i * 0.1 + 0.05, b: 0.1 })) })
            compare(many.regions.length, 4, "at most four regions")
        }
        function test_save_import_and_delete_own_presets() {
            veyra.exclusionState = ({ enabled: true, feather: 4, regions: [{ left: 0.1, top: 0.1, right: 0.3, bottom: 0.3, ellipse: true }] })
            veyra.fgEnabled = true; veyra.outputRateMode = 2; veyra.outputCustomFps = 120
            verify(games.saveMine("My Racer"))
            const stored = JSON.parse(veyra.preferences.pushGamePresets)
            compare(stored.length, 1)
            compare(stored[0].target, "120")
            compare(stored[0].regions[0].ellipse, true)
            compare(games.selectedName, "My Racer")
            verify(games.selected.mine)
            verify(!games.saveMine("GTA VI"), "built-in names are reserved")
            const code = games.shareCode(games.selected)
            games.deleteMine("My Racer")
            compare(JSON.parse(veyra.preferences.pushGamePresets).length, 0)
            verify(games.addImported(Presets.decode(code, Qt.atob)))
            compare(JSON.parse(veyra.preferences.pushGamePresets)[0].name, "My Racer")
            verify(!games.addImported(null))
            verify(games.messageError)
            verify(games.addImported(Presets.decode(games.shareCode(Presets.builtIn[0]), Qt.atob)))
            verify(games.all.some(p => p.name.indexOf("GTA VI ") === 0 && p.mine), "a shared copy of a built-in gets its own name")
        }
        function test_copy_button() {
            const b = findChild(games, "pushfps-game-preset-copy")
            verify(b !== null)
            b.clicked()
            verify(veyra.lastCopiedText.indexOf("PUSHFPS1:") === 0)
        }

        // --- latency readout -------------------------------------------------
        function test_latency_badge() {
            verify(!badge.visible, "off by default")
            veyra.preferences = ({ pushLatencyOverlay: true })
            veyra.liveStats = ({ running: true, latencyMs: 14.4, latencyP95Ms: 19.6, submitFps: 119.7, submitLowFps: 112.2 })
            badge.refresh()
            verify(badge.visible)
            compare(findChild(badge, "pushfps-latency-ms").text, qsTr("延迟 %1 ms").arg("14"))
            compare(badge.level, Theme.accent)
            veyra.liveStats = ({ running: true, latencyMs: 75 })
            badge.refresh()
            verify(badge.level !== Theme.accent)
            veyra.liveStats = ({ running: true })
            badge.refresh()
            compare(findChild(badge, "pushfps-latency-ms").text, qsTr("延迟 测量中"))
        }

        // --- capture card list -------------------------------------------------
        function test_capture_card_status() {
            const row = findChild(quick, "pushfps-card-row")
            verify(!row.visible, "no card, no row")
            veyra.captureDeviceLabel = "Game Capture 4K X"
            verify(row.visible)
            compare(findChild(quick, "pushfps-card-status").text, qsTr("已测试"))
            veyra.captureDeviceLabel = "USB Video"
            compare(findChild(quick, "pushfps-card-status").text, qsTr("未测试"))
        }
        function test_tools_open_their_dialogs() {
            const list = []
            const handler = p => list.push(p)
            veyra.navigate.connect(handler)
            findChild(quick, "pushfps-optimize").clicked()
            findChild(quick, "pushfps-game-presets").clicked()
            veyra.navigate.disconnect(handler)
            verify(list.indexOf("optimize") >= 0 && list.indexOf("games") >= 0, list.join(","))
            compare(list[0], "pro", "the picture is shown before it is measured")
        }

        // --- Discord ------------------------------------------------------------
        function test_share_to_discord_without_webhook() {
            card.show("C:/Videos/PushFPS/PushFPS-Replay-1.mp4", true)
            tryVerify(() => card.opacity > 0.99, 2000)
            const b = findChild(card, "pushfps-recording-discord")
            mouseClick(b)
            compare(veyra.lastCopiedFile, "C:/Videos/PushFPS/PushFPS-Replay-1.mp4")
            compare(veyra.discordOpened, 1)
            verify(card.shareText.length > 0)
            card.on = false
        }
        function test_send_to_discord_with_webhook() {
            veyra.preferences = ({ pushDiscordWebhook: "https://discord.com/api/webhooks/1/abc" })
            card.show("C:/r.mp4", false)
            tryVerify(() => card.opacity > 0.99, 2000)
            mouseClick(findChild(card, "pushfps-recording-discord"))
            compare(veyra.lastSharedFile, "C:/r.mp4")
            verify(card.sending)
            veyra.pushShareFinished(true, "sent")
            verify(!card.sending)
            compare(card.shareText, "sent")
            card.on = false
        }
    }
}
