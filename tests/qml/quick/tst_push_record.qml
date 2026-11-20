// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// The record dialog (PushRecordDialog, PushRecord.js) against the fixture bridge: the shape, fill or
// fit, the size and the picture quality are the recording preferences Settings has, the dialog says
// what a recording started now will be, its start button records, and what Settings chose is shown
// and left alone until something is picked here.
import QtQuick
import QtTest
import Veyra
import "../qml/Veyra/PushRecord.js" as Record
Item {
    id: root
    width: 900; height: 900
    Item { id: host; anchors.fill: parent; property string dialog: ""; property real motionScale: 1; function close() { dialog = "" } }
    PushRecordDialog { id: rec; dialogHost: host }

    TestCase {
        name: "PushRecord"
        when: windowShown
        function init() {
            host.dialog = ""
            veyra.preferences = ({})
            veyra.hasSource = true
            veyra.recording = false
            veyra.recordingStarts = 0; veyra.recordingSource = ""
            veyra.liveStats = ({ running: true, outputWidth: 3840, outputHeight: 2160, realFps: 30, generatedFps: 30, hdr: false })
            host.dialog = "record"
            verify(rec.shown)
        }
        function control(name) { const c = findChild(rec, name); verify(c !== null, name); return c }
        function action(label) { rec.actionTriggered(label) }

        function test_nothing_chosen_is_the_picture_as_it_is_at_the_standard_rate() {
            compare(rec.shapeId, "wide"); compare(rec.sizeId, 0); compare(rec.qualityId, "high")
            compare(rec.plan.width, 3840); compare(rec.plan.height, 2160); compare(rec.plan.fps, 60)
            compare(rec.plan.mbps, 149, "the recorder's standard rate for 4K at 60")
            verify(control("pushfps-record-summary").text.indexOf("3840") >= 0 && control("pushfps-record-summary").text.indexOf("149") >= 0)
            tryVerify(function() { return control("pushfps-record-size-row").visible }, 3000, "the dialog is on screen")
            verify(!rec.tall && !control("pushfps-record-fill-row").visible, "fill or fit is a question for a tall recording only")
            action(qsTr("开始录制"))
            compare(veyra.recordingStarts, 1); compare(veyra.recordingSource, "record-dialog")
            compare(host.dialog, "", "the dialog closes when the recording starts")
        }
        function test_the_rule_is_the_recorders() {
            // The numbers of engine/PushRecordQuality.h (its unit test has the same ones).
            compare(Record.autoMbps(1920, 1080, 60, false, 100), 37)
            compare(Record.autoMbps(2560, 1440, 60, false, 100), 66)
            compare(Record.autoMbps(3840, 2160, 60, false, 100), 149)
            compare(Record.autoMbps(1920, 1080, 120, false, 100), 56)
            compare(Record.autoMbps(3840, 2160, 60, false, 50), 75)
            compare(Record.autoMbps(1920, 1080, 60, false, 50), 19)
            compare(Record.autoMbps(3840, 2160, 120, false, 100), 160, "never past the recorder's limit")
            compare(Record.autoMbps(640, 360, 30, true, 100), 8)
        }
        function test_a_tall_recording_fills_or_fits() {
            control("pushfps-record-shape").picked("tall")
            compare(veyra.preferences.recordWidth, 1080); compare(veyra.preferences.recordHeight, 1920)
            compare(veyra.preferences.recordFit, 1, "a tall recording starts filled, as the phone apps show it")
            verify(rec.tall && rec.fills)
            // (A dialog fades in: what it shows is visible a moment after it opens.)
            tryVerify(function() { return control("pushfps-record-fill-row").visible }, 3000, "a tall recording asks fill or fit")
            const picture = control("pushfps-record-preview-picture"), frame = control("pushfps-record-preview")
            // (The frame takes its new shape at the next layout pass, so these wait for it.)
            tryVerify(function() { return frame.height > frame.width && picture.width > frame.width }, 2000, "filled: the picture's sides are cut")
            control("pushfps-record-fill").picked("fit")
            compare(veyra.preferences.recordFit, 0); verify(!rec.fills)
            tryVerify(function() { return picture.width <= frame.width + 0.5 && picture.height < frame.height }, 2000, "fitted: the whole picture, bars above and below")
            // The size of a tall recording, kept when the shape is picked again.
            control("pushfps-record-size").picked("1440")
            compare(veyra.preferences.recordWidth, 1440); compare(veyra.preferences.recordHeight, 2560)
            compare(veyra.preferences.recordFit, 0, "fit stays chosen")
            control("pushfps-record-shape").picked("tall")
            compare(veyra.preferences.recordWidth, 1440)
            // Back to wide: the picture's own size again, nothing cut.
            control("pushfps-record-shape").picked("wide")
            compare(veyra.preferences.recordWidth, 0); compare(veyra.preferences.recordHeight, 0); compare(veyra.preferences.recordFit, 0)
        }
        function test_sizes() {
            control("pushfps-record-size").picked("1440")
            compare(veyra.preferences.recordWidth, 2560); compare(veyra.preferences.recordHeight, 1440)
            compare(rec.plan.mbps, 66)
            control("pushfps-record-size").picked("1080")
            compare(veyra.preferences.recordWidth, 1920); compare(rec.plan.mbps, 37)
            verify(!rec.enlarges)
            control("pushfps-record-size").picked("0")
            compare(veyra.preferences.recordWidth, 0); compare(rec.sizeId, 0)
            // A size larger than the picture is said.
            veyra.liveStats = ({ running: true, outputWidth: 1920, outputHeight: 1080, realFps: 60, generatedFps: 60 })
            host.dialog = ""; host.dialog = "record"
            control("pushfps-record-size").picked("2160")
            verify(rec.enlarges)
            const row = control("pushfps-record-size-row"), enlarged = row.hint
            control("pushfps-record-size").picked("1080")
            verify(!rec.enlarges && row.hint !== enlarged)
            // A tall, filled recording is as high as the picture is: from a 1080p picture 1080 x 1920 is
            // enlarged, from a 4K picture it is not.
            control("pushfps-record-shape").picked("tall")
            verify(rec.enlarges, "1080 x 1920 filled from a 1920 x 1080 picture is enlarged")
            veyra.liveStats = ({ running: true, outputWidth: 3840, outputHeight: 2160, realFps: 30, generatedFps: 30 })
            host.dialog = ""; host.dialog = "record"
            verify(!rec.enlarges, "and from a 4K picture it is not")
        }
        function test_picture_quality() {
            control("pushfps-record-quality").picked("best")
            compare(veyra.preferences.recordAdvanced, true); compare(veyra.preferences.recordRateControl, 2)
            compare(veyra.preferences.recordQuality, Record.bestLevel)
            compare(rec.qualityId, "best"); compare(rec.plan.mbps, 0)
            const best = control("pushfps-record-summary").text
            control("pushfps-record-quality").picked("small")
            compare(veyra.preferences.pushRecordBitrateScale, 50)
            compare(veyra.preferences.recordRateControl, 1, "back to a rate: the standard rate control")
            compare(rec.qualityId, "small"); compare(rec.plan.mbps, 75)
            verify(control("pushfps-record-summary").text !== best)
            control("pushfps-record-quality").picked("high")
            compare(veyra.preferences.pushRecordBitrateScale, 100)
            compare(rec.qualityId, "high"); compare(rec.plan.mbps, 149)
        }
        function test_what_settings_chose_is_shown_and_kept_until_something_is_picked() {
            // A platform profile from Settings: 4K at 60 Mbps.
            veyra.preferences = ({ recordProfile: "yt-2160p60", recordWidth: 3840, recordHeight: 2160, recordBitrateKbps: 60000, recordAdvanced: true, recordRateControl: 1 })
            compare(rec.qualityId, "custom"); compare(rec.plan.mbps, 60)
            verify(control("pushfps-record-summary").text.indexOf("60 Mbps") >= 0, "the low rate is said before it is recorded with")
            action(qsTr("开始录制"))
            compare(veyra.preferences.recordBitrateKbps, 60000, "starting changes nothing")
            compare(veyra.preferences.recordProfile, "yt-2160p60")
            // Picking a quality here takes over: the standard rate, and the profile no longer applies.
            veyra.recording = false; host.dialog = "record"
            control("pushfps-record-quality").picked("high")
            compare(veyra.preferences.recordBitrateKbps, 0); compare(veyra.preferences.recordProfile, "")
            compare(rec.plan.mbps, 149)
            // Another shape or size from Settings is shown as it is.
            veyra.preferences = ({ recordWidth: 3440, recordHeight: 1440 })
            compare(rec.sizeId, -1); compare(rec.shapeId, "wide"); compare(rec.plan.width, 3440)
        }
        function test_asking_is_a_choice_and_nothing_starts_without_a_picture() {
            verify(rec.asks && Record.asks(veyra.preferences))
            control("pushfps-record-ask").toggled(false)
            compare(veyra.preferences.pushRecordAsk, false); verify(!Record.asks(veyra.preferences))
            control("pushfps-record-ask").toggled(true)
            verify(Record.asks(veyra.preferences))
            veyra.hasSource = false
            action(qsTr("开始录制"))
            compare(veyra.recordingStarts, 0); compare(host.dialog, "record")
            action(qsTr("取消"))
            compare(host.dialog, "")
        }
        // The vertical copy: a second file beside a wide recording, off unless switched on.
        function test_a_vertical_copy_beside_a_wide_recording() {
            const row = control("pushfps-record-vertical-row"), toggle = control("pushfps-record-vertical"), fit = control("pushfps-record-vertical-fit")
            const summary = control("pushfps-record-summary"), line = qsTr("另存一份竖屏副本：1080 × 1920，最高 60 FPS")
            tryVerify(function() { return row.visible }, 3000)
            verify(!rec.copy && !fit.visible, "off unless switched on")
            verify(summary.text.indexOf(line) < 0)
            toggle.toggled(true)
            compare(veyra.preferences.pushRecordVerticalCopy, true)
            verify(rec.copy && rec.copyFills, "the copy fills the tall frame unless chosen otherwise")
            tryVerify(function() { return fit.visible }, 3000)
            verify(summary.text.indexOf(line) >= 0, "what will be recorded says so")
            fit.picked("fit")
            compare(veyra.preferences.pushRecordVerticalFit, 0); verify(!rec.copyFills)
            fit.picked("fill")
            compare(veyra.preferences.pushRecordVerticalFit, 1); verify(rec.copyFills)
            // A tall recording is the vertical one already: no copy of it is offered.
            control("pushfps-record-shape").picked("tall")
            verify(rec.tall)
            tryVerify(function() { return !row.visible }, 3000)
            verify(summary.text.indexOf(line) < 0)
            compare(veyra.preferences.pushRecordVerticalCopy, true, "the choice is kept for the next wide recording")
        }
    }
}
