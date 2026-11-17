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
        function test_record_replay_compare_screenshot() {
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
            studio.activate("brb")
            compare(veyra.lastScene.type, "card")
            compare(studio.activeScene, "brb")
            compare(veyra.preferences.mixGameMuted, true, "a card mutes the game")
            verify(findChild(studio, "studio-scene-brb").active)
            studio.activate("soon")
            compare(veyra.preferences.mixGameMuted, true)
            studio.activate("game")
            compare(veyra.lastScene.type, "game")
            compare(veyra.preferences.mixGameMuted, false, "back to the game: the mute from before")
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
            compare(desktop.subtitle, qsTr("除 PushFPS 外的所有声音"))
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
    }
}
