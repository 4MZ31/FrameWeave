// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// Comparison clip dialog (PushCompareDialog) against the fixture bridge: the layout and the
// length are kept in preferences, the start button records with them, the preview puts the
// FPS labels where the clip does, and the quick setup tile shows the choice.
import QtQuick
import QtTest
import Veyra
Item {
    id: root
    width: 900; height: 1400
    Item { id: host; anchors.fill: parent; property string dialog: ""; property real motionScale: 1; function close() { dialog = "" } }
    PushCompareDialog { id: cmp; dialogHost: host }
    PushQuickSetup { id: quick; width: 560; y: 900 }

    TestCase {
        name: "PushCompare"
        when: windowShown
        function init() {
            host.dialog = ""
            veyra.hasSource = true
            veyra.recording = false
            veyra.recordingState = ({ saved: 0, active: false, path: "" })
            veyra.compareStarts = 0; veyra.compareSeconds = 0; veyra.compareLayout = -1
            veyra.setPreference("pushCompareLayout", 0)
            veyra.setPreference("pushCompareSeconds", 15)
            veyra.setPreference("pushCompareUntilStop", false)
            veyra.setPreference("pushGameFps", 30)
            host.dialog = "compare"
            verify(cmp.shown)
        }
        function action(label) { cmp.actionTriggered(label) }

        function test_defaults_split_and_15_seconds() {
            compare(cmp.layoutId, 0)
            compare(cmp.seconds, 15)
            action(qsTr("开始录制"))
            compare(veyra.compareStarts, 1)
            compare(veyra.compareSeconds, 15)
            compare(veyra.compareLayout, 0)
            compare(host.dialog, "", "the dialog closes once recording starts")
        }
        function test_side_by_side_and_length_are_kept_and_used() {
            findChild(cmp, "pushfps-compare-layout").picked("1")
            findChild(cmp, "pushfps-compare-length").picked("30")
            compare(veyra.preferences.pushCompareLayout, 1)
            compare(veyra.preferences.pushCompareSeconds, 30)
            action(qsTr("开始录制"))
            compare(veyra.compareSeconds, 30)
            compare(veyra.compareLayout, 1)
        }
        function test_vertical_for_short_video_apps() {
            findChild(cmp, "pushfps-compare-layout").picked("2")
            compare(veyra.preferences.pushCompareLayout, 2)
            compare(cmp.layoutId, 2)
            verify(cmp.verticalPreview, "portrait preview shown")
            verify(findChild(cmp, "pushfps-compare-preview-vertical") !== null)
            // Labels next to the divider: the console's just above, PushFPS's just below (192-high preview).
            const top = cmp.verticalTagY(0, 12, 192), bottom = cmp.verticalTagY(1, 12, 192)
            verify(top + 12 <= 96 && top > 60, "console label just above the middle, y=" + top)
            verify(bottom >= 96 && bottom < 120, "PushFPS label just below the middle, y=" + bottom)
            verify(cmp.layoutHint(2).indexOf("TikTok") >= 0)
            action(qsTr("开始录制"))
            compare(veyra.compareLayout, 2)
            const tile = findChild(quick, "pushfps-compare-clip")
            compare(tile.sub, qsTr("%1 秒").arg(15) + " · " + qsTr("竖屏"))
        }
        function test_lengths_up_to_two_minutes() {
            const length = findChild(cmp, "pushfps-compare-length")
            compare(length.options.map(o => o.id).join(","), "10,15,30,60,90,120", "the lengths there were, then 90 and 120 seconds")
            for (const seconds of [90, 120, 10]) {
                length.picked(String(seconds))
                compare(veyra.preferences.pushCompareSeconds, seconds)
                compare(cmp.seconds, seconds)
                action(qsTr("开始录制"))
                compare(veyra.compareSeconds, seconds)
                host.dialog = "compare"
            }
            compare(veyra.compareStarts, 3)
            veyra.setPreference("pushCompareSeconds", 120)
            compare(findChild(quick, "pushfps-compare-clip").sub, qsTr("%1 秒").arg(120) + " · " + qsTr("分屏"))
        }
        function test_until_i_stop_in_every_layout() {
            const toggle = findChild(cmp, "pushfps-compare-until-stop")
            verify(toggle !== null, "the dialog has an until-I-stop switch")
            const length = findChild(cmp, "pushfps-compare-length")
            findChild(cmp, "pushfps-compare-length").picked("30")
            toggle.toggled(true)
            compare(veyra.preferences.pushCompareUntilStop, true)
            verify(cmp.untilStop)
            verify(!length.enabled, "the length waits while there is none")
            compare(veyra.preferences.pushCompareSeconds, 30, "and is kept for later")
            // Split, side by side and vertical all start without a length.
            for (const layout of [0, 1, 2]) {
                findChild(cmp, "pushfps-compare-layout").picked(String(layout))
                action(qsTr("开始录制"))
                compare(veyra.compareLayout, layout)
                compare(veyra.compareSeconds, 0, "0 seconds asks for a clip that runs until it is stopped (layout " + layout + ")")
                host.dialog = "compare"
            }
            compare(veyra.compareStarts, 3)
            const tile = findChild(quick, "pushfps-compare-clip")
            compare(tile.sub, qsTr("手动停止") + " · " + qsTr("竖屏"))
            // Switched off again: the length from before.
            toggle.toggled(false)
            verify(length.enabled)
            action(qsTr("开始录制"))
            compare(veyra.compareSeconds, 30)
            compare(tile.sub, qsTr("%1 秒").arg(30) + " · " + qsTr("竖屏"))
        }
        function test_a_running_clip_shows_its_time_and_stops_from_the_tile() {
            veyra.setPreference("pushCompareUntilStop", true)
            const tile = findChild(quick, "pushfps-compare-clip")
            veyra.recording = true
            veyra.recordingState = ({ saved: 0, active: true, path: "", compare: true, seconds: 83.4 })
            compare(tile.text, qsTr("停止对比片段"))
            compare(tile.sub, qsTr("正在录制…") + " 01:23", "how long it has run: a clip without a length has no other clock")
            verify(tile.enabled)
            veyra.recordingState = ({ saved: 0, active: true, path: "", compare: true, seconds: 3725 })
            compare(tile.sub, qsTr("正在录制…") + " 1:02:05")
            const stops = veyra.recordingStops
            tile.clicked()
            compare(veyra.recordingStops, stops + 1, "the tile stops it")
            verify(!veyra.recording)
            veyra.recordingState = ({ saved: 0, active: false, path: "" })
        }
        function test_an_unknown_layout_falls_back_to_split() {
            veyra.setPreference("pushCompareLayout", 5)
            compare(cmp.layoutId, 0)
        }
        function test_cancel_records_nothing() {
            action(qsTr("取消"))
            compare(veyra.compareStarts, 0)
            compare(host.dialog, "")
        }
        function test_an_unknown_length_falls_back_to_15() {
            veyra.setPreference("pushCompareSeconds", 7)
            compare(cmp.seconds, 15)
            veyra.setPreference("pushCompareSeconds", 0)
            compare(cmp.seconds, 15, "no length is its own switch, never a length of 0")
            action(qsTr("开始录制"))
            compare(veyra.compareSeconds, 15)
        }
        function test_preview_labels_follow_the_layout() {
            // Split: the top corners of the one picture (320 x 180 preview, 90-wide labels).
            compare(cmp.tagX(0, 90, 320, false), 6, "split: left label at the top left")
            compare(cmp.tagX(1, 90, 320, false), 224, "split: right label at the top right")
            compare(cmp.tagY(16, 180, false), 6, "split: labels at the top of the picture")
            // Side by side: centred over each half, in the black bar (180 / 4 = 45 high) above the pictures.
            compare(cmp.tagX(0, 90, 320, true) + 45, 80, "side by side: centred over the left picture")
            compare(cmp.tagX(1, 90, 320, true) + 45, 240, "side by side: centred over the right picture")
            const y = cmp.tagY(16, 180, true)
            verify(y >= 0 && y + 16 <= 45, "side by side: labels sit in the black bar, y=" + y)
            verify(findChild(cmp, "pushfps-compare-preview") !== null, "the preview is drawn")
        }
        function test_preview_labels_name_both_rates() {
            veyra.fgMultiplier = 4
            compare(cmp.leftTag, qsTr("主机原画 · %1 FPS").arg(30))
            compare(cmp.rightTag, "PushFPS · 120 FPS")
            veyra.fgMultiplier = 1
        }
        function test_tile_opens_the_dialog_with_nothing_playing() {
            veyra.hasSource = false
            const tile = findChild(quick, "pushfps-compare-clip")
            verify(tile.enabled, "the layout and length can be chosen before the capture card runs")
            const opened = []
            const onOpen = p => opened.push(p)
            veyra.navigate.connect(onOpen)
            tile.clicked()
            veyra.navigate.disconnect(onOpen)
            compare(opened.join(","), "compare")
            compare(veyra.compareStarts, 0)
            veyra.hasSource = true
        }
        function test_tile_shows_the_choice() {
            veyra.setPreference("pushCompareLayout", 1)
            veyra.setPreference("pushCompareSeconds", 60)
            const tile = findChild(quick, "pushfps-compare-clip")
            compare(tile.sub, qsTr("%1 秒").arg(60) + " · " + qsTr("并排"))
        }
    }
}
