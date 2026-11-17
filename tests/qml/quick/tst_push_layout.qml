// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// The layout editor (PushLayoutDialog: move, resize keeping proportions, Ctrl for a free shape,
// undo/redo, crop, background), its maths (PushLayout.js) and the output presets
// (PushOutputPresets.js) in the stream dialog and the recording settings, against the fixture bridge.
import QtQuick
import QtTest
import Veyra
import "../qml/Veyra/PushLayout.js" as LayoutMath
import "../qml/Veyra/PushOutputPresets.js" as Presets
Item {
    id: root
    width: 1000; height: 1000
    Item { id: host; anchors.fill: parent; property string dialog: ""; function close() { dialog = "" } }
    PushLayoutDialog { id: lay; dialogHost: host; frameAspect: 16 / 9 }
    PushStreamDialog { id: live; dialogHost: host }

    function cam(extra) {
        return Object.assign({ id: "c1", kind: "camera", target: "cam1", name: "USB Webcam", corner: "br", size: 0.25, shape: "rect" }, extra || {})
    }
    function scenesWith(sources) {
        return JSON.stringify([{ id: "game", name: "Game", type: "game", sources: sources }, { id: "brb", name: "BRB", type: "card" }])
    }
    function savedSources() { return JSON.parse(String(veyra.preferences.studioScenes))[0].sources }
    function center(item) { return item.mapToItem(root, item.width / 2, item.height / 2) }

    TestCase {
        name: "PushLayout"
        when: windowShown
        function init() {
            host.dialog = ""
            veyra.preferences = ({ exportStopsPlayback: false, streamWidth: 1920, streamHeight: 1080, studioScenes: root.scenesWith([root.cam()]) })
            veyra.sourceStatus = []
            lay.output = "stream"
            lay.sceneId = "game"
        }
        function open() {
            host.dialog = "layout"
            verify(lay.shown)
            // Pointer tests need the dialog where it ends up, not mid open-animation.
            tryCompare(lay, "motionS", 1, 3000)
            tryCompare(lay, "motionDy", 0, 3000)
            waitForRendering(lay)
        }

        // --- maths ---------------------------------------------------------------------------
        function test_resize_keeps_proportions_unless_free() {
            const start = { x: 0.5, y: 0.5, w: 0.2, h: 0.1 }
            const kept = LayoutMath.resizeRect(start, 3, 0.9, 0.62, true, 1)
            fuzzyCompare(kept.w / kept.h, 2, 1e-9)
            fuzzyCompare(kept.x, 0.5, 1e-9)
            fuzzyCompare(kept.w, 0.4, 1e-9, "the larger pull wins")
            const free = LayoutMath.resizeRect(start, 3, 0.9, 0.62, false, 1)
            fuzzyCompare(free.w, 0.4, 1e-9); fuzzyCompare(free.h, 0.12, 1e-9)
            const topLeft = LayoutMath.resizeRect(start, 0, 0.6, 0.55, true, 1)
            fuzzyCompare(topLeft.x + topLeft.w, 0.7, 1e-9, "the opposite corner stays")
            fuzzyCompare(topLeft.y + topLeft.h, 0.6, 1e-9)
            fuzzyCompare(topLeft.w / topLeft.h, 2, 1e-9)
        }
        function test_move_snaps_and_stays_on_the_picture() {
            const r = LayoutMath.moveRect({ x: 0.2, y: 0.2, w: 0.3, h: 0.3 }, -0.195, 0.0, true)
            fuzzyCompare(r.x, 0, 1e-9, "snapped to the left edge")
            const far = LayoutMath.moveRect({ x: 0.2, y: 0.2, w: 0.3, h: 0.3 }, 5, 5, false)
            verify(far.x <= 0.98 && far.y <= 0.98)
        }
        function test_source_rect_matches_the_engine() {
            // 16:9 camera at 25 % width on a 16:9 output, bottom-right with the default margin.
            const r = LayoutMath.sourceRect(root.cam(), 16 / 9, 16 / 9)
            fuzzyCompare(r.w, 0.25, 1e-9); fuzzyCompare(r.h, 0.25, 1e-9)
            fuzzyCompare(r.x, 1 - 0.025 * 9 / 16 - 0.25, 1e-9)
            fuzzyCompare(r.y, 1 - 0.025 - 0.25, 1e-9)
            // On a vertical output the same width is taller.
            const v = LayoutMath.sourceRect(root.cam({ corner: "custom", x: 0, y: 0 }), 16 / 9, 9 / 16)
            fuzzyCompare(v.h, 0.25 * (9 / 16) / (16 / 9), 1e-9)
            const stretched = LayoutMath.sourceRect(root.cam({ corner: "custom", x: 0.1, y: 0.1, h: 0.5 }), 16 / 9, 16 / 9)
            fuzzyCompare(stretched.h, 0.5, 1e-9)
        }
        function test_canvas_map_fit_and_fill() {
            const fit = LayoutMath.mapCanvas({ x: 0, y: 0, w: 1, h: 1 }, {}, 0, 16 / 9, 9 / 16)
            fuzzyCompare(fit.dst.w, 1, 1e-9)
            fuzzyCompare(fit.dst.h, (9 / 16) / (16 / 9), 1e-9, "bars above and below")
            const fill = LayoutMath.mapCanvas({ x: 0, y: 0, w: 1, h: 1 }, {}, 1, 16 / 9, 9 / 16)
            fuzzyCompare(fill.dst.h, 1, 1e-9)
            fuzzyCompare(fill.src.w, (9 / 16) / (16 / 9), 1e-9, "the middle of the picture")
            fuzzyCompare(fill.src.x, (1 - fill.src.w) / 2, 1e-9)
        }
        function test_history_undo_redo() {
            const h = LayoutMath.history(3)
            verify(h.push({ a: 1 }))
            verify(!h.push({ a: 1 }), "the same state twice is one step")
            h.push({ a: 2 })
            compare(h.undo({ a: 3 }).a, 2)
            compare(h.redo({ a: 2 }).a, 3)
            h.push({ a: 4 }); h.push({ a: 5 }); h.push({ a: 6 })
            compare(h.items.length, 3, "limited")
        }

        // --- the dialog ------------------------------------------------------------------------
        function test_drag_moves_a_source_and_undo_restores_it() {
            open()
            const item = findChild(lay, "layout-item-1")
            verify(item)
            const p = center(item)
            mousePress(root, p.x, p.y)
            mouseMove(root, p.x - 120, p.y - 80)
            mouseRelease(root, p.x - 120, p.y - 80)
            compare(lay.selected, 1)
            const moved = root.savedSources()[0]
            compare(moved.corner, "custom")
            compare(moved.h, 0, "a move keeps the camera's own shape")
            verify(moved.x < 0.7, JSON.stringify(moved))
            verify(lay.canUndo)
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            compare(root.savedSources()[0].corner, "br", "Ctrl+Z puts it back")
            verify(lay.canRedo)
            keyClick(Qt.Key_Y, Qt.ControlModifier)
            compare(root.savedSources()[0].corner, "custom", "Ctrl+Y redoes")
        }
        function test_resize_keeps_the_shape_and_ctrl_stretches() {
            open()
            lay.selected = 1
            waitForRendering(lay)
            const item = findChild(lay, "layout-item-1")
            const handle = findChild(item, "layout-handle-0")
            verify(handle)
            let p = handle.mapToItem(root, 6, 6)
            mousePress(root, p.x, p.y)
            mouseMove(root, p.x - 60, p.y - 10)
            mouseRelease(root, p.x - 60, p.y - 10)
            let s = root.savedSources()[0]
            compare(s.h, 0, "proportions kept")
            verify(s.size > 0.25, JSON.stringify(s))
            // Ctrl: the shape follows the pointer.
            waitForRendering(lay)
            const h2 = findChild(findChild(lay, "layout-item-1"), "layout-handle-3")
            p = h2.mapToItem(root, 6, 6)
            mousePress(root, p.x, p.y)
            mouseMove(root, p.x, p.y + 90, -1, Qt.LeftButton, Qt.ControlModifier)
            mouseRelease(root, p.x, p.y + 90, Qt.LeftButton, Qt.ControlModifier)
            s = root.savedSources()[0]
            verify(s.h > 0, "a stretched source keeps its height: " + JSON.stringify(s))
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            compare(root.savedSources()[0].h, 0, "undo")
        }
        function test_game_picture_box_crop_and_background() {
            open()
            lay.selected = 0
            waitForRendering(lay)
            const handle = findChild(findChild(lay, "layout-item-0"), "layout-handle-3")
            // The full-frame picture's corner handle sits on the stage's clipped edge: press its inner half.
            const p = handle.mapToItem(root, 2, 2)
            mousePress(root, p.x, p.y)
            mouseMove(root, p.x - 300, p.y - 100)
            mouseRelease(root, p.x - 300, p.y - 100)
            tryVerify(() => Number(veyra.preferences.canvasW) < 1000, 2000, "the box shrank: " + veyra.preferences.canvasW)
            fuzzyCompare(Number(veyra.preferences.canvasW) / Number(veyra.preferences.canvasH), 1, 0.02, "16:9 box on a 16:9 output keeps its shape")
            verify(veyra.preferences.canvasStretch !== true)
            lay.setCrop("l", 0.2); lay.commit()
            compare(Number(veyra.preferences.canvasCropL), 200)
            const swatch = findChild(lay, "layout-bg-8df237")
            verify(swatch, "background swatch")
            // The swatch can sit below the dialog's visible area at this window size: trigger its handler.
            swatch.children[0].clicked(null)
            compare(Number(veyra.preferences.canvasBackground), 0x8df237)
            mouseClick(findChild(lay, "layout-reset"))
            compare(Number(veyra.preferences.canvasW), 1000)
            compare(Number(veyra.preferences.canvasCropL), 0)
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            verify(Number(veyra.preferences.canvasW) < 1000, "reset undone")
        }
        function test_vertical_output_shape() {
            veyra.setPreference("streamWidth", 1080); veyra.setPreference("streamHeight", 1920)
            open()
            const stage = findChild(lay, "layout-stage")
            verify(stage.height > stage.width, "the stage has the output's shape")
            const game = findChild(lay, "layout-item-0")
            verify(game.height < stage.height / 2, "fit: the 16:9 picture with bars above and below")
        }

        // --- presets -------------------------------------------------------------------------
        function test_every_preset_is_valid() {
            for (const pl of Presets.streamPlatforms) for (const p of pl.presets) {
                verify(p.kbps >= 500 && p.kbps <= 51000, p.id)
                verify(p.fps === 30 || p.fps === 60, p.id)
                verify(p.width <= 4096 && p.height <= 4096 && p.width % 2 === 0 && p.height % 2 === 0, p.id)
                compare(p.keyframe, 2, p.id)
                verify(p.audioKbps >= 64 && p.audioKbps <= 320, p.id)
            }
            for (const pl of Presets.recordPlatforms) for (const p of pl.presets) {
                verify(p.rate === 2 || (p.kbps >= 300 && p.kbps <= 400000), p.id)
                verify(p.audioKbps >= 64 && p.audioKbps <= 320, p.id)
                verify(p.width <= 8192 && p.height <= 8192, p.id)
            }
            // Platform limits (OBS rtmp-services, as the fixture's platform table).
            for (const p of Presets.streamPlatforms.find(x => x.id === "twitch").presets) verify(p.kbps <= 6000 && p.audioKbps <= 160, p.id)
            for (const p of Presets.streamPlatforms.find(x => x.id === "kick").presets) verify(p.kbps <= 8000, p.id)
            for (const p of Presets.streamPlatforms.find(x => x.id === "tiktok").presets) verify(p.kbps <= 6000 && p.height > p.width, p.id)
            compare(Presets.streamPreset("youtube-1080p60").kbps, 17000)
            compare(Presets.recordPreset("yt-2160p60").kbps, 60000)
        }
        function test_stream_preset_fills_settings_and_shows_changes() {
            host.dialog = "stream"
            verify(live.shown)
            verify(live.applyPreset("twitch-1080p60"))
            compare(Number(veyra.preferences.streamBitrateKbps), 6000)
            compare(Number(veyra.preferences.streamWidth), 1920)
            compare(Number(veyra.preferences.streamAudioKbps), 160)
            compare(live.presetChanges.length, 0)
            veyra.setPreference("streamBitrateKbps", 5000)
            compare(live.presetChanges.length, 1, "a tweak keeps the preset, marked modified")
            compare(live.preset.id, "twitch-1080p60")
            verify(live.applyPreset("tiktok-1080x1920-30"))
            compare(Number(veyra.preferences.streamHeight), 1920)
            compare(live.bitrate, 4.5)
        }
        function test_record_preset_prefs() {
            const prefs = Presets.recordPrefs(Presets.recordPreset("ythdr-2160p60"))
            compare(prefs.recordHevc, true)
            compare(prefs.recordAdvanced, true, "advanced settings carry the preset's encoding")
            compare(prefs.recordBitrateKbps, 75000)
            const merged = Object.assign({}, prefs)
            compare(Presets.differences(merged, Presets.recordPreset("ythdr-2160p60"), false).length, 0)
            merged.recordAudioKbps = 192
            compare(Presets.differences(merged, Presets.recordPreset("ythdr-2160p60"), false)[0], "recordAudioKbps")
            const edit = Presets.recordPrefs(Presets.recordPreset("edit-source"))
            compare(edit.recordRateControl, 2)
            compare(edit.recordWidth, 0, "editing keeps the picture's own size")
        }
    }
}
