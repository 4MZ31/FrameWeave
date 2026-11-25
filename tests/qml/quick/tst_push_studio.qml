// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS streaming mode (StudioPage, PushMixerStrip, PushMicFiltersDialog) against the fixture
// bridge: the OBS-style layout, the mixer's faders and mutes, the microphone's destinations and
// filters, go live / record controls and the meters' ballistics.
import QtQuick
import QtQuick.Window
import QtTest
import Veyra
import "../qml/Veyra/StudioLayout.js" as StudioGeometry
Item {
    id: root
    width: 1400; height: 900
    Item { id: fixture; anchors.fill: parent }
    Item { id: host; anchors.fill: parent; property string dialog: ""; property real motionScale: 1; function close() { dialog = "" } }
    // VPage takes its parent from Main.qml's page stack; the fixture places it directly.
    StudioPage { id: studio; pageId: "live"; parent: fixture; width: fixture.width; height: fixture.height; visible: true; enabled: true }
    PushMicFiltersDialog { id: filters; dialogHost: host }

    // A window as Main.qml is one, for the header's maximize button.
    QtObject {
        id: fakeShell
        property bool maximized: false
        property bool fullscreen: false
        property int toggles: 0
        function toggleMaximized() { toggles++; maximized = !maximized }
    }
    property var navigated: []
    Connections { target: veyra; function onNavigate(p) { root.navigated.push(p) } }

    TestCase {
        name: "PushStudio"
        when: windowShown
        function init() {
            veyra.preferences = ({exportStopsPlayback: false})
            veyra.hasSource = true
            veyra.recording = false
            veyra.volume = 1; veyra.muted = false
            veyra.streamState = ({active: false, live: false, outputs: []})
            veyra.streamStarts = 0
            studio.activeScene = "game"
            studio.setCardMute(false)
            root.navigated = []
            host.dialog = ""
        }
        function control(item, name) { const c = findChild(item, name); verify(c !== null, name); return c }

        function test_layout_like_obs() {
            const g = StudioGeometry.calculate(1400, 900)
            verify(!g.compact)
            verify(g.left.x < g.preview.x && g.preview.x + g.preview.width < g.right.x, "features | preview | controls")
            verify(g.mixer.y >= g.preview.y + g.preview.height && g.status.y >= g.mixer.y + g.mixer.height, "mixer and status bar below")
            compare(g.mixer.width, 1400 - 24, "mixer spans the window")
            verify(g.video.y > g.preview.y && g.video.height < g.preview.height, "the picture sits under the preview header")
            const small = StudioGeometry.calculate(900, 600)
            verify(small.compact && small.left.width === 0, "narrow window: no feature column")
            verify(small.preview.width > 300)
            compare(studio.videoViewport.width, g.video.width)
        }
        function test_meter_scale_and_fall() {
            compare(StudioGeometry.meterPosition(0), 1)
            compare(StudioGeometry.meterPosition(-60), 0)
            compare(StudioGeometry.meterPosition(-30), 0.5)
            compare(StudioGeometry.meterPosition(-200), 0)
            compare(StudioGeometry.fall(-10, -40, 0.5, 24), -22, "falls 24 dB/s")
            compare(StudioGeometry.fall(-30, -6, 0.05, 24), -6, "rises at once")
            compare(StudioGeometry.duration(3725), "1:02:05")
            compare(StudioGeometry.duration(65), "1:05")
        }
        function test_game_fader_and_mute() {
            const game = control(studio, "mixer-game")
            game.moved(-6.4)
            compare(veyra.preferences.mixGameDb, -6)
            compare(game.readout, "-6.0 dB")
            game.muteToggled()
            compare(veyra.preferences.mixGameMuted, true)
            compare(game.readout, qsTr("静音"))
            game.muteToggled()
            compare(veyra.preferences.mixGameMuted, false)
            game.moved(-60)
            compare(game.readout, "-∞ dB")
        }
        function test_microphone_destinations_and_filters() {
            const mic = control(studio, "mixer-mic")
            verify(!studio.micUsed)
            compare(mic.subtitle, qsTr("未用于录制或直播"))
            verify(control(studio, "mixer-mic-record") !== null)
            studio.toggleMicRecording()
            compare(veyra.preferences.recordMic, true)
            verify(studio.micUsed)
            mic.moved(4)
            compare(veyra.preferences.mixMicDb, 4)
            mic.gearClicked()
            compare(root.navigated[root.navigated.length - 1], "micfilters")
        }
        function test_monitor_is_the_player_volume() {
            const monitor = control(studio, "mixer-monitor")
            monitor.moved(0.4)
            compare(veyra.volume, 0.4)
            compare(monitor.readout, "40%")
            monitor.muteToggled()
            verify(veyra.muted)
        }
        function test_go_live_needs_an_output() {
            control(studio, "studio-go-live").clicked()
            compare(root.navigated[root.navigated.length - 1], "stream", "no platform yet: the outputs dialog opens")
            veyra.setPreference("streamTargets", JSON.stringify([{id: "a", platform: "twitch", name: "Twitch", enabled: true}]))
            veyra.pushSetStreamKey("a", "k")
            control(studio, "studio-go-live").clicked()
            compare(veyra.streamStarts, 1)
            verify(studio.live)
            verify(control(studio, "studio-live-badge").visible)
            compare(studio.streamKbps, 6100)
            control(studio, "studio-go-live").clicked()
            verify(!studio.live)
        }
        // The header's maximize button (PushMaximizeButton): it does what the window's own does, says
        // which way it goes, and is not there without a window that can be maximized.
        function test_maximize_button_in_the_header() {
            const b = control(studio, "studio-maximize")
            verify(!b.usable && !b.visible, "the test's own window cannot be maximized this way: no button")
            b.shell = fakeShell
            verify(b.usable && b.visible && !b.maximized)
            const maximize = b.tip
            b.clicked()
            compare(fakeShell.toggles, 1); verify(b.maximized)
            verify(b.tip !== maximize, "it now says restore")
            b.clicked()
            compare(fakeShell.toggles, 2); verify(!b.maximized)
            compare(b.tip, maximize)
            // Full screen counts as large: the button then leads back, as the window's own does.
            fakeShell.fullscreen = true
            verify(b.maximized)
            fakeShell.fullscreen = false
        }
        function test_record_replay_compare_screenshot() {
            // The choices first (dialog "record"), unless asking is switched off.
            control(studio, "studio-record").clicked()
            verify(!veyra.recording); compare(root.navigated[root.navigated.length - 1], "record")
            veyra.setPreference("pushRecordAsk", false)
            control(studio, "studio-record").clicked()
            verify(veyra.recording)
            verify(control(studio, "studio-rec-badge").visible)
            control(studio, "studio-replay").clicked()
            compare(veyra.preferences.replayEnabled, true, "first tap turns the instant replay on")
            control(studio, "studio-replay").clicked()
            compare(veyra.replaySaves, 1)
            control(studio, "studio-compare").clicked()
            compare(root.navigated[root.navigated.length - 1], "compare")
            const before = veyra.screenshots
            control(studio, "studio-screenshot").clicked()
            compare(veyra.screenshots, before + 1)
            veyra.recording = false
        }
        // Clip markers: a button while a recording runs, never for a comparison clip, counted on the button.
        function test_a_moment_is_marked_while_recording() {
            const state = veyra.recordingState, added = veyra.markersAdded
            const mark = control(studio, "studio-marker")
            veyra.recording = false
            verify(!mark.visible, "nothing to mark without a recording")
            veyra.recording = true
            veyra.recordingState = ({saved: 0, active: true, path: "C:/Videos/FrameWeave/rec.mp4"})
            tryVerify(function() { return mark.visible }, 3000)
            compare(mark.count, 0)
            mark.clicked(); mark.clicked()
            compare(veyra.markersAdded, added + 2)
            compare(mark.count, 2, "the button says how many this recording has")
            verify(mark.text.indexOf("2") >= 0)
            veyra.recordingState = ({saved: 0, active: true, path: "C:/Videos/FrameWeave/clip.mp4", compare: true})
            verify(!mark.visible, "a comparison clip takes no markers")
            veyra.recording = false; veyra.recordingState = state
        }
        function test_meters_follow_the_levels() {
            veyra.studioLevels = ({gamePeakDb: -3, gameRmsDb: -12, micPeakDb: -100, micRmsDb: -100, micOpen: false})
            studio.lastTick = 0
            studio.poll()
            compare(studio.gamePeak, -3)
            compare(studio.gameRms, -12)
            veyra.studioLevels = ({gamePeakDb: -100, gameRmsDb: -100, micPeakDb: -100, micRmsDb: -100, micOpen: false})
            studio.lastTick = Date.now() - 500
            studio.poll()
            verify(studio.gamePeak < -3 && studio.gamePeak > -100, "falls off instead of dropping to silence")
        }
        function test_scenes_switch_the_output_and_mute_the_game_on_cards() {
            compare(studio.scenes.length, 4, "game, starting soon, be right back, thanks for watching")
            veyra.setPreference("mixGameMuted", false)
            const game = control(studio, "mixer-game")
            studio.activate("brb")
            compare(veyra.lastScene.type, "card")
            compare(studio.activeScene, "brb")
            compare(veyra.cardMute, true, "a card mutes the game")
            compare(veyra.preferences.mixGameMuted, false, "and never through the mixer's saved mute: closing FrameWeave on a card left that on")
            compare(game.muted, true, "the strip shows it")
            compare(game.subtitle, qsTr("图卡场景显示时静音"))
            verify(findChild(studio, "studio-scene-brb").active)
            studio.activate("soon")
            compare(veyra.cardMute, true)
            compare(veyra.preferences.mixGameMuted, false)
            studio.activate("game")
            compare(veyra.lastScene.type, "game")
            compare(veyra.cardMute, false, "back to the game: heard again")
            compare(veyra.preferences.mixGameMuted, false)
            compare(game.muted, false)
        }
        function test_a_card_keeps_the_mixers_own_mute() {
            const game = control(studio, "mixer-game")
            veyra.setPreference("mixGameMuted", true)
            studio.activate("brb")
            studio.activate("game")
            compare(veyra.cardMute, false)
            compare(veyra.preferences.mixGameMuted, true, "a mute the user set stays through a card")
            compare(game.muted, true)
            veyra.setPreference("mixGameMuted", false)
        }
        function test_unmuting_under_a_card_lets_the_game_be_heard() {
            const game = control(studio, "mixer-game")
            veyra.setPreference("mixGameMuted", false)
            studio.activate("brb")
            game.muteToggled()
            compare(veyra.cardMute, false, "unmute lifts the card's mute")
            compare(veyra.preferences.mixGameMuted, false)
            compare(game.muted, false)
            game.muteToggled()
            compare(veyra.preferences.mixGameMuted, true, "and the next press is the mixer's own mute")
            compare(veyra.cardMute, false)
            veyra.setPreference("mixGameMuted", false)
            studio.activate("game")
        }
        function test_a_card_that_keeps_the_game_sound() {
            veyra.setPreference("mixGameMuted", false)
            studio.addScene("card")
            const id = studio.editingScene
            studio.updateScene(id, { muteGame: false })
            studio.activate(id)
            compare(veyra.cardMute, false, "a card set to keep the game's sound")
            studio.updateScene(id, { muteGame: true })
            tryCompare(veyra, "cardMute", true)
            studio.activate("game")
            compare(veyra.cardMute, false)
            studio.removeScene(id)
        }
        function test_scenes_add_edit_remove() {
            studio.addScene("card")
            compare(studio.scenes.length, 5)
            const id = studio.editingScene
            verify(id.length > 0, "the new scene opens in the editor")
            verify(findChild(studio, "studio-scene-editor").visible)
            studio.updateScene(id, { title: "Back in 5", subtitle: "Grab a drink" })
            compare(studio.sceneById(id).title, "Back in 5")
            studio.activate(id)
            const calls = veyra.sceneCalls
            studio.updateScene(id, { subtitle: "Soon" })
            wait(20)
            verify(veyra.sceneCalls > calls, "editing the live scene redraws it")
            verify(studio.removeScene(id))
            compare(studio.activeScene, "game", "removing the live scene goes back to the game")
            verify(!studio.removeScene("game"), "the only game scene stays")
            studio.addScene("game")
            const g = studio.editingScene
            veyra.pickedImage = "C:/logo.png"
            studio.updateScene(g, { overlayImage: veyra.pickedImage, overlayText: "@me", corner: "bl" })
            studio.activate(g)
            compare(veyra.lastScene.overlayText, "@me")
            compare(veyra.lastScene.corner, "bl")
            verify(studio.removeScene(g))
            studio.editingScene = ""
        }
        function test_sources_in_a_scene() {
            veyra.recording = false
            studio.addScene("game")
            const g = studio.editingScene
            verify(studio.addSource("camera"))
            const cam = studio.sceneById(g).sources[0]
            compare(cam.kind, "camera")
            compare(cam.target, "cam1")
            compare(cam.name, "USB Webcam")
            compare(cam.shape, "round", "a camera starts with rounded corners")
            compare(cam.corner, "br")
            verify(findChild(studio, "studio-source-" + cam.id) !== null, "the source is listed in the editor")
            verify(studio.addSource("window"))
            compare(studio.sceneById(g).sources[1].target, "4242")
            veyra.pickedMedia = ""
            verify(!studio.addSource("media"), "a cancelled file dialog adds nothing")
            veyra.pickedMedia = "C:/clips/intro.mp4"
            verify(studio.addSource("media"))
            compare(studio.sceneById(g).sources[2].name, "intro.mp4")
            verify(studio.addSource("camera"))
            verify(!studio.addSource("camera"), "at most four sources")
            // Sources run only while this page is on screen or something records or streams.
            studio.activate(g)
            compare(veyra.lastSources.length, 0, "not on screen, nothing recording: no camera on")
            veyra.recording = true
            wait(20)
            compare(veyra.lastSources.length, 4, "recording: the scene's sources run")
            studio.updateSource(cam.id, { corner: "tl", size: 0.4, mirror: true })
            wait(20)
            compare(veyra.lastSources[0].corner, "tl")
            compare(veyra.lastSources[0].size, 0.4)
            compare(veyra.lastSources[0].mirror, true)
            studio.removeSource(cam.id)
            wait(20)
            compare(veyra.lastSources.length, 3)
            studio.activate("game")
            compare(veyra.lastSources.length, 0, "the game scene has none")
            veyra.recording = false
            verify(studio.removeScene(g))
            studio.editingScene = ""
        }
        function test_the_comparison_button_stops_a_running_clip() {
            const button = control(studio, "studio-compare")
            compare(button.text, qsTr("对比片段"))
            veyra.recording = true
            veyra.recordingState = ({ saved: 0, active: true, path: "", compare: true, seconds: 12 })
            compare(button.text, qsTr("停止对比片段"), "a clip without a length is stopped where it was started")
            const stops = veyra.recordingStops
            button.clicked()
            compare(veyra.recordingStops, stops + 1)
            verify(!veyra.recording)
            veyra.recordingState = ({ saved: 0, active: false, path: "" })
            compare(button.text, qsTr("对比片段"))
        }
        function test_scene_menu_adds_copies_and_deletes() {
            const menu = findChild(studio, "studio-scene-menu")
            verify(menu !== null, "the scenes list has a right-click menu")
            const find = id => menu.items.findIndex(x => x.id === id)
            const pick = id => menu.picked(find(id), menu.items[find(id)])
            const count = studio.scenes.length
            // Beside the scenes: only the two ways to add one.
            menu.targetId = ""
            compare(menu.items.length, 2)
            compare(menu.items[0].id, "add-game")
            pick("add-card")
            compare(studio.scenes.length, count + 1, "a card scene is added")
            const card = studio.editingScene
            compare(studio.sceneById(card).type, "card")
            studio.editingScene = ""
            // On a scene: switch to it, edit, copy, delete, then the two ways to add.
            menu.targetId = card
            compare(menu.items.length, 7)
            compare(menu.title, studio.sceneById(card).name)
            pick("show")
            compare(studio.activeScene, card)
            verify(menu.items[find("show")].disabled === true, "the scene on now is not switched to again")
            pick("edit")
            compare(studio.editingScene, card)
            studio.updateScene(card, { title: "Break", sources: [{ id: "srcA", kind: "camera", target: "cam1", name: "USB Webcam" }] })
            wait(20)
            pick("copy")
            compare(studio.scenes.length, count + 2)
            const copy = studio.editingScene
            verify(copy !== card && copy.length > 0, "the copy opens in the editor")
            compare(studio.scenes.findIndex(x => x.id === copy), studio.scenes.findIndex(x => x.id === card) + 1, "right under the original")
            compare(studio.sceneById(copy).title, "Break")
            compare(studio.sceneById(copy).sources[0].target, "cam1")
            verify(studio.sceneById(copy).sources[0].id !== "srcA", "its sources get ids of their own")
            menu.targetId = copy
            pick("delete")
            verify(studio.sceneById(copy) === null, "deleted from the menu")
            compare(studio.editingScene, "", "the editor closes with its scene")
            menu.targetId = card
            pick("delete")
            compare(studio.scenes.length, count)
            compare(studio.activeScene, "game", "deleting the scene on now goes back to the game")
            compare(veyra.cardMute, false, "and the card's mute goes with it")
            // The only game scene stays.
            menu.targetId = "game"
            verify(menu.items[find("delete")].disabled === true)
            compare(menu.items[find("delete")].note, qsTr("至少保留一个画面场景"))
            studio.sceneMenuPick("delete", "game")
            verify(studio.sceneById("game") !== null)
            // With a second game scene the first can go, and the other takes over.
            menu.targetId = ""
            pick("add-game")
            const second = studio.editingScene
            studio.editingScene = ""
            menu.targetId = "game"
            verify(menu.items[find("delete")].disabled !== true)
            verify(studio.duplicateScene("missing") === "", "nothing to copy, nothing copied")
            verify(studio.removeScene(second))
        }
        function test_the_scene_under_the_pointer() {
            const card = control(studio, "studio-controls")
            const row = control(studio, "studio-scene-brb")
            const onRow = row.mapToItem(card, row.width / 2, row.height / 2)
            compare(studio.sceneAt(Qt.point(onRow.x, onRow.y)), "brb", "a right-click on a scene is about that scene")
            const add = control(studio, "studio-add-card-scene")
            const beside = add.mapToItem(card, add.width / 2, add.height / 2)
            compare(studio.sceneAt(Qt.point(beside.x, beside.y)), "", "beside the scenes: the list's own menu")
            const live = control(studio, "studio-go-live")
            const below = live.mapToItem(card, live.width / 2, live.height / 2)
            verify(studio.sceneAt(Qt.point(below.x, below.y)) === null, "the buttons under the list are not the scenes list")
        }
        function test_camera_resolution_and_live_preview() {
            veyra.recording = true
            veyra.sourceSerial = 0
            studio.addScene("game")
            const g = studio.editingScene
            verify(studio.addSource("camera"))
            const cam = studio.sceneById(g).sources[0]
            studio.activate(g)
            studio.editingSource = cam.id
            wait(30)
            let row = control(studio, "studio-source-" + cam.id)
            compare(control(row, "source-resolution").current, "0", "automatic until another is chosen")
            control(row, "source-resolution").picked("2160")
            wait(30)
            compare(studio.sceneById(g).sources[0].resolution, 2160)
            compare(veyra.lastSources[0].resolution, 2160, "the camera is asked for 4K")
            row = control(studio, "studio-source-" + cam.id)
            compare(control(row, "source-resolution").current, "2160")
            // No frame yet: no picture is asked for.
            const preview = control(row, "source-preview")
            compare(String(preview.source), "")
            verify(!preview.visible)
            // What the camera offers is listed, largest first as the bridge gives it.
            compare(studio.cameraSizes(null), "")
            const offered = studio.cameraSizes({ sizes: [{ width: 1920, height: 1080, fps: 30 }, { width: 1728, height: 3072, fps: 59.94 }] })
            verify(offered.indexOf("1920×1080 (30 FPS), 1728×3072 (60 FPS)") >= 0, offered)
            veyra.sourceStatus = [{ id: cam.id, running: true, error: "", width: 1920, height: 1080, fps: 30, sizes: [{ width: 1920, height: 1080, fps: 30 }] }]
            studio.pollSlow()
            wait(30)
            row = control(studio, "studio-source-" + cam.id)
            verify(control(row, "source-sizes").visible, "under the camera's settings")
            veyra.sourceStatus = []
            studio.pollSlow()
            studio.editingSource = ""
            studio.activate("game")
            veyra.recording = false
            verify(studio.removeScene(g))
            studio.editingScene = ""
        }
        function test_no_camera_found() {
            studio.addScene("game")
            const g = studio.editingScene
            const before = veyra.cameras
            veyra.cameras = []
            verify(!studio.addSource("camera"))
            compare(studio.sourceNote, qsTr("没有找到摄像头"))
            veyra.cameras = before
            verify(studio.removeScene(g))
            studio.editingScene = ""
        }
        function test_transition_and_extra_audio() {
            const t = control(studio, "studio-transition")
            t.picked("800")
            compare(veyra.preferences.studioTransitionMs, 800)
            compare(t.current, "800")
            t.picked("0")
            compare(studio.transitionMs, 0)
            const desktop = control(studio, "mixer-desktop")
            compare(desktop.subtitle, qsTr("关闭：不进入录制和直播"))
            veyra.setPreference("mixDesktopOn", true)
            compare(desktop.subtitle, qsTr("除 FrameWeave 外的所有声音"))
            desktop.moved(-12.3)
            compare(veyra.preferences.mixDesktopDb, -12)
            desktop.muteToggled()
            compare(veyra.preferences.mixDesktopMuted, true)
            const aux = control(studio, "mixer-aux")
            veyra.setPreference("mixAuxOn", true)
            control(studio, "mixer-aux-device").picked("mic2")
            compare(veyra.preferences.mixAuxDevice, "mic2")
            aux.moved(3)
            compare(veyra.preferences.mixAuxDb, 3)
            verify(control(studio, "mixer-game") && control(studio, "mixer-mic") && control(studio, "mixer-monitor"), "all five strips")
        }
        function test_mic_filters_dialog() {
            host.dialog = "micfilters"
            verify(filters.shown)
            control(filters, "mic-highpass").toggled(true)
            compare(veyra.preferences.micHighPass, true)
            verify(filters.responseDb(30) < -10, "rumble filter in the curve")
            control(filters, "mic-gate").toggled(true)
            compare(veyra.preferences.micGate, true)
            filters.applyPreset("clear")
            compare(veyra.preferences.micEq, "-2,-3,0,4,2")
            compare(veyra.preferences.micEqOn, true)
            compare(filters.presetOf(filters.eq), qsTr("人声清晰"))
            verify(Math.abs(filters.responseDb(3500) - 4) < 1.5, "EQ boost shows in the curve")
            filters.setBand(0, 5)
            compare(filters.presetOf(filters.eq), qsTr("自定义"))
            filters.actionTriggered(qsTr("全部关闭"))
            compare(veyra.preferences.micHighPass, false)
            compare(veyra.preferences.micGate, false)
            compare(veyra.preferences.micEqOn, false)
            filters.actionTriggered(qsTr("完成"))
            compare(host.dialog, "")
        }

        function test_pushflow_is_chosen_in_streaming_mode() {
            veyra.fgBackendChoices = [{ id: "pushflow", label: "PushFlow" }, { id: "dlss", label: "DLSS" }]
            veyra.fgBackendName = "dlss"
            const row = control(studio, "pushfps-fg-backend-row")
            verify(row.visible, "the frame generation method is chosen in streaming mode")
            verify(!control(studio, "pushfps-pushflow-tier-row").visible, "tiers only for PushFlow")
            control(studio, "pushfps-fg-backend").picked("pushflow")
            compare(veyra.fgBackendName, "pushflow")
            verify(control(studio, "pushfps-pushflow-tier-row").visible)
            control(studio, "pushfps-pushflow-tier").picked("2")
            compare(veyra.vfgQuality, 2)
            veyra.fgBackendName = "dlss"; veyra.vfgQuality = 1
            veyra.fgBackendChoices = [{ id: "dlss", label: "DLSS" }]
        }
    }
}
