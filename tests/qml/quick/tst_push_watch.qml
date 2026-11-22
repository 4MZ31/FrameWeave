// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS video playback mode (WatchPage) against the fixture bridge: picking a window, a screen or a
// file, starting the capture without game mode, and the four picture switches.
import QtQuick
import QtTest
import Veyra
Item {
    id: root
    width: 1300; height: 820
    Item { id: fixture; anchors.fill: parent }
    WatchPage { id: watch; pageId: "watch"; parent: fixture; width: fixture.width; height: fixture.height; visible: true; enabled: true }
    TestCase {
        name: "PushWatch"
        when: windowShown
        function init() {
            veyra.preferences = ({exportStopsPlayback: false})
            veyra.hasSource = false
            veyra.screenStarts = 0; veyra.fileDialogs = 0
            veyra.fgEnabled = false; veyra.fgMultiplier = 1; veyra.contentRate = 0
            veyra.srEnabled = false; veyra.videoHdr = false
            veyra.screenOptions = ({kind: 0, passthrough: true, method: 0, fps: 0})
            watch.kind = 0
        }
        function control(name) { const c = findChild(watch, name); verify(c !== null, name); return c }
        function test_window_capture_starts_without_game_mode() {
            watch.pick(0)
            control("watch-start").clicked()
            compare(veyra.screenStarts, 1)
            compare(veyra.screenOptions.passthrough, false, "a plain copy of the picture, no input passthrough")
            verify(veyra.hasSource)
            control("watch-stop").clicked()
            verify(!veyra.hasSource)
        }
        // Frame generation already on (e.g. from a console) without duplicate removal: starting a
        // video switches it to Auto, or generated frames would sit between identical pictures.
        function test_start_turns_on_duplicate_removal_for_video() {
            veyra.fgEnabled = true; veyra.contentRate = 0
            watch.pick(0)
            control("watch-start").clicked()
            compare(veyra.contentRate, 1)
            control("watch-stop").clicked()
            veyra.fgEnabled = false
        }
        function test_screen_and_file() {
            watch.pick(1)
            compare(veyra.screenOptions.kind, 1)
            watch.pick(2)
            control("watch-start").clicked()
            compare(veyra.fileDialogs, 1)
            compare(veyra.screenStarts, 0)
        }
        function test_smoother_sets_frame_generation_and_duplicate_removal() {
            control("watch-smooth").toggled(true)
            verify(veyra.fgEnabled)
            compare(veyra.fgMultiplier, 2)
            compare(veyra.contentRate, 1, "videos repeat frames: duplicate removal finds the real ones")
            control("watch-smooth").toggled(false)
            verify(!veyra.fgEnabled)
        }
        function test_sharper_enhance_hdr() {
            control("watch-sharp").toggled(true)
            verify(veyra.srEnabled)
            control("watch-hdr").toggled(true)
            verify(veyra.videoHdr)
            watch.setEnhancement(true)
            verify(veyra.nrAnyEnabled)
            watch.setEnhancement(false)
        }
        // 4.2.1: "Sharper" is switched on with an upscaler this card can run (PushUpscale.js): with one it
        // cannot run the engine switches the stage off again, and the switch did nothing on such a card.
        function test_sharper_takes_an_upscaler_the_card_can_run() {
            const wasChoices = veyra.srBackendChoices, wasQuality = veyra.videoSrQuality
            veyra.srEnabled = false
            veyra.videoSrQuality = 2
            control("watch-sharp").toggled(true)
            verify(veyra.srEnabled)
            compare(veyra.videoSrQuality, 2, "an upscaler the card runs is kept")
            control("watch-sharp").toggled(false)
            verify(!veyra.srEnabled)
            veyra.srBackendChoices = [{id: "0", label: "DLSS SR", disabled: true}, {id: "rtx", label: "RTX", disabled: true},
                                      {id: "5", label: "FSR 3.1 / 4"}, {id: "6", label: "NVIDIA Image Scaling (NIS)"}]
            // The bridge answers "sr" for the upscaler chosen now, which this card cannot run.
            const wasCaps = veyra.effectCapabilities
            veyra.effectCapabilities = Object.assign({}, wasCaps, {sr: {available: false, reason: "needs NVIDIA"}})
            verify(watch.srOk && control("watch-sharp").enabled, "the switch is usable when any upscaler can run")
            veyra.effectCapabilities = wasCaps
            control("watch-sharp").toggled(true)
            verify(veyra.srEnabled)
            compare(veyra.videoSrQuality, 5, "a card without the NVIDIA upscalers gets FSR")
            control("watch-sharp").toggled(false)
            compare(veyra.videoSrQuality, 5, "switching off changes nothing else")
            veyra.srBackendChoices = wasChoices; veyra.videoSrQuality = wasQuality; veyra.srEnabled = false
        }
        SignalSpy { id: navigations; target: veyra; signalName: "navigate" }
        SignalSpy { id: settingsRequests; target: watch; signalName: "requestSettings" }
        // Recording as in Professional mode: record, comparison clip (same dialog), instant replay,
        // audio and microphone, and the shared recording settings.
        function test_recording_section() {
            veyra.recording = false
            const record = control("watch-record")
            verify(!record.enabled, "nothing to record without a picture")
            veyra.hasSource = true
            verify(record.enabled)
            const toggles = veyra.recordingToggles
            // The choices first (dialog "record"); nothing is recorded by the click itself.
            veyra.setPreference("pushRecordAsk", true)
            navigations.clear()
            record.clicked()
            compare(navigations.count, 1); compare(navigations.signalArguments[0][0], "record")
            compare(veyra.recordingToggles, toggles); verify(!veyra.recording)
            // With asking switched off the button records at once, as before 4.0.
            veyra.setPreference("pushRecordAsk", false)
            record.clicked()
            compare(veyra.recordingToggles, toggles + 1)
            verify(veyra.recording)
            // Clip markers: the row is there only while a recording runs.
            const markerRow = control("watch-marker-row"), added = veyra.markersAdded
            tryVerify(function() { return markerRow.visible }, 3000)
            control("watch-marker").clicked()
            compare(veyra.markersAdded, added + 1)
            compare(markerRow.count, 1)
            // And it always stops at once, asking or not.
            veyra.setPreference("pushRecordAsk", true)
            record.clicked()
            compare(veyra.recordingToggles, toggles + 2); verify(!veyra.recording)
            verify(!markerRow.visible, "no recording, nothing to mark")
            veyra.setPreference("pushRecordAsk", false)
            record.clicked(); verify(veyra.recording)
            verify(!control("watch-compare").enabled, "no comparison clip while recording")
            record.clicked()
            verify(!veyra.recording)

            navigations.clear()
            control("watch-compare").clicked()
            compare(navigations.count, 1)
            compare(navigations.signalArguments[0][0], "compare", "the same comparison clip dialog as Professional mode")

            control("watch-replay").toggled(true)
            compare(veyra.preferences.replayEnabled, true)
            verify(control("watch-replay-save").visible)
            control("watch-replay").toggled(false)

            control("watch-recaudio").toggled(false)
            compare(veyra.preferences.recordAudio, false)
            control("watch-recaudio").toggled(true)
            verify(control("watch-recdesktop").enabled)
            control("watch-recdesktop").toggled(true)
            compare(veyra.preferences.mixDesktopOn, true, "the video's sound from the browser or player")
            control("watch-recmic").toggled(true)
            compare(veyra.preferences.recordMic, true)

            settingsRequests.clear()
            control("watch-record-settings").clicked()
            compare(settingsRequests.count, 1)
            compare(settingsRequests.signalArguments[0][1], "set-recording")
            veyra.hasSource = false
        }
        function test_viewport_beside_the_controls() {
            verify(watch.videoViewport.x > watch.panelWidth)
            verify(watch.videoViewport.width > 300)
        }
    }
}
