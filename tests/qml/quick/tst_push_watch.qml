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
            record.clicked()
            compare(veyra.recordingToggles, toggles + 1)
            verify(veyra.recording)
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
