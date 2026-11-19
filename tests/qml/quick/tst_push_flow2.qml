// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFlow 2, the parts a user sees (3.2.7): the frame quality test says its worst frame and its worst
// place (PushQualityTestDialog), and PushFlow's motion size can be chosen apart from its tier
// (PushFlowMotionRow, preference pushFlowMotion), against the fixture bridge.
import QtQuick
import QtTest
import Veyra
Item {
    id: root
    width: 900; height: 900
    Item { id: host; anchors.fill: parent; property string dialog: ""; function close() { dialog = "" } }
    PushQualityTestDialog { id: quality; dialogHost: host }
    PushFlowMotionRow { id: motion; width: 620; y: 20 }

    TestCase {
        name: "PushFlow2"
        when: windowShown
        function init() {
            host.dialog = ""
            veyra.preferences = ({})
            veyra.hasSource = false
            veyra.fgEnabled = true
            veyra.fgBackendName = "pushflow"
            veyra.liveStats = ({ running: false })
            veyra.qualityModel = null
            veyra.qualityState = ({ running: false, done: false, failed: false })
            quality.st = ({})
            motion.readLive()
        }
        function cleanup() { veyra.qualityModel = null; veyra.fgBackendName = "dlss"; veyra.hasSource = true }
        function control(item, name) { const c = findChild(item, name); verify(c !== null, name); return c }

        // --- the frame quality test's worst frame and worst place -------------------------------
        function test_quality_test_says_its_worst_frame_and_place() {
            veyra.qualityModel = function(backend) {
                return { score: 82, psnr: 31.4, realPsnr: 48.0, blendPsnr: 27.0, gainOverBlend: 4.4, worstFramePsnr: 24.6, worstPlacePsnr: 11.3 }
            }
            host.dialog = "qualitytest"
            verify(quality.shown)
            quality.begin()
            tryCompare(quality, "running", false)
            const frame = control(quality, "quality-test-worst-frame"), place = control(quality, "quality-test-worst-place")
            tryCompare(frame, "visible", true, 3000)
            compare(frame.value, "24.6 dB")
            verify(place.visible)
            compare(place.value, "11.3 dB")
            // The score row is the one it always was: the two new rows add to it and change nothing in it.
            compare(control(quality, "quality-test-score").value, "82 / 100")
        }
        function test_a_result_without_them_shows_no_empty_rows() {
            // What an optimiser record or an older bridge gives: no worst frame, no worst place.
            veyra.qualityModel = function(backend) { return { score: 70, psnr: 30, realPsnr: 47, blendPsnr: 26, gainOverBlend: 4 } }
            host.dialog = "qualitytest"
            quality.begin()
            tryCompare(control(quality, "quality-test-score"), "visible", true, 3000)
            verify(!control(quality, "quality-test-worst-frame").visible)
            verify(!control(quality, "quality-test-worst-place").visible)
        }

        // --- PushFlow's motion size ----------------------------------------------------------------
        function test_motion_size_is_automatic_until_chosen() {
            const seg = control(motion, "list-pushflow-motion")
            compare(motion.chosen, 0)
            compare(seg.current, "0")
            compare(seg.options.length, 4)
            verify(veyra.preferences.pushFlowMotion === undefined, "showing the row writes nothing")
            verify(motion.hint.length > 0 && motion.help.length > 0)
        }
        function test_choosing_a_motion_size_writes_the_preference() {
            const seg = control(motion, "list-pushflow-motion")
            seg.picked("720")
            compare(veyra.preferences.pushFlowMotion, 720)
            compare(motion.chosen, 720)
            compare(seg.current, "720")
            seg.picked("1080")
            compare(veyra.preferences.pushFlowMotion, 1080)
            seg.picked("0")
            compare(veyra.preferences.pushFlowMotion, 0)
            compare(motion.chosen, 0)
        }
        function test_a_size_that_does_not_exist_reads_as_automatic() {
            veyra.setPreference("pushFlowMotion", 900)
            compare(motion.chosen, 0)
            compare(control(motion, "list-pushflow-motion").current, "0")
            veyra.setPreference("pushFlowMotion", "abc")
            compare(motion.chosen, 0)
        }
        function test_the_row_says_the_size_really_in_use() {
            veyra.setPreference("pushFlowMotion", 720)
            const chosenHint = motion.hint
            verify(chosenHint.indexOf("720") >= 0)
            // PushFlow runs: the hint is what the network works at, here a 3440 x 1440 picture at 720 lines.
            veyra.liveStats = ({ running: true, pushFlowMotionWidth: 1488, pushFlowMotionHeight: 624 })
            veyra.hasSource = true
            tryCompare(motion, "liveWidth", 1488, 3000)
            compare(motion.liveHeight, 624)
            verify(motion.hint.indexOf("1488") >= 0 && motion.hint.indexOf("624") >= 0)
            // Another method, or nothing playing: the size is gone and the hint is the choice again.
            veyra.liveStats = ({ running: true, pushFlowMotionWidth: 0, pushFlowMotionHeight: 0 })
            tryCompare(motion, "liveWidth", 0, 3000)
            compare(motion.hint, chosenHint)
            veyra.liveStats = ({ running: true, pushFlowMotionWidth: 960, pushFlowMotionHeight: 540 })
            tryCompare(motion, "liveWidth", 960, 3000)
            veyra.hasSource = false
            tryCompare(motion, "liveWidth", 0, 3000)
        }
    }
}
