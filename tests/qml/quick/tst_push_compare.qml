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
        function test_cancel_records_nothing() {
            action(qsTr("取消"))
            compare(veyra.compareStarts, 0)
            compare(host.dialog, "")
        }
        function test_an_unknown_length_falls_back_to_15() {
            veyra.setPreference("pushCompareSeconds", 7)
            compare(cmp.seconds, 15)
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
        function test_tile_shows_the_choice() {
            veyra.setPreference("pushCompareLayout", 1)
            veyra.setPreference("pushCompareSeconds", 60)
            const tile = findChild(quick, "pushfps-compare-clip")
            compare(tile.sub, qsTr("%1 秒").arg(60) + " · " + qsTr("并排"))
        }
    }
}
