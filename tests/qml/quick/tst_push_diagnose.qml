// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// Check my setup (PushDiagnoseDialog, rules in PushDiagnose.js), the quick setup's tile for it and
// NVIDIA VFG's quality row, against the fixture bridge. The rules themselves are also run with Node
// (tests/js/push_diagnose_test.js); here it is what the dialog reads, shows, changes and takes back.
import QtQuick
import QtTest
import Veyra
import "../qml/Veyra/PushDiagnose.js" as Diagnose
Item {
    id: root
    width: 900; height: 1400
    Item { id: host; anchors.fill: parent; property string dialog: ""; function close() { dialog = "" } }
    PushDiagnoseDialog { id: diag; dialogHost: host; sampleCount: 1; sampleInterval: 10 }
    PushQuickSetup { id: quick; width: 520 }
    property string navigated: ""
    Connections { target: veyra; function onNavigate(p) { root.navigated = p } }

    TestCase {
        name: "PushDiagnose"
        when: windowShown
        property var saved: ({})
        function init() {
            saved = {
                hasSource: veyra.hasSource, sourceKind: veyra.sourceKind, sourceHeight: veyra.sourceHeight, fgEnabled: veyra.fgEnabled,
                fgMultiplier: veyra.fgMultiplier, fgBackendName: veyra.fgBackendName, vfgQuality: veyra.vfgQuality, nrAnyEnabled: veyra.nrAnyEnabled,
                nrLayers: veyra.nrLayers, preferences: veyra.preferences, liveStats: veyra.liveStats, outputRateMode: veyra.outputRateMode,
                outputCustomFps: veyra.outputCustomFps, displayRefreshHz: veyra.displayRefreshHz, displaySync: veyra.displaySync, vrrState: veyra.vrrState,
                sharper: veyra.captureSharperFormat, audioChoice: veyra.captureAudioChoice, audioSuggestion: veyra.captureAudioSuggestion,
                choices: veyra.fgBackendChoices
            }
            host.dialog = ""; root.navigated = ""
            // A 4K console picture at 30 FPS, NVIDIA VFG on Medium at 4X to 120 on a 120 Hz screen
            // with variable refresh, enhancement at the standard size: two things to say.
            veyra.hasSource = true; veyra.sourceKind = "capture"; veyra.sourceHeight = 2160
            veyra.fgEnabled = true; veyra.fgMultiplier = 4; veyra.fgBackendName = "vfg"; veyra.vfgQuality = 1
            veyra.nrAnyEnabled = true; veyra.nrLayers = [{ index: 1, sizePolicy: 0, runtime: 2 }]
            veyra.outputRateMode = 2; veyra.outputCustomFps = 120; veyra.displayRefreshHz = 120; veyra.displaySync = 1; veyra.vrrState = 2
            veyra.liveStats = ({ running: true, contentFps: 30, realFps: 30, generatedFps: 90, fgIdle: 0, fgMultiplierNow: 4 })
            veyra.captureSharperFormat = []; veyra.captureAudioChoice = -2; veyra.captureAudioSuggestion = -2
        }
        function cleanup() {
            host.dialog = ""
            veyra.hasSource = saved.hasSource; veyra.sourceKind = saved.sourceKind; veyra.sourceHeight = saved.sourceHeight; veyra.fgEnabled = saved.fgEnabled
            veyra.fgMultiplier = saved.fgMultiplier; veyra.fgBackendName = saved.fgBackendName; veyra.vfgQuality = saved.vfgQuality; veyra.nrAnyEnabled = saved.nrAnyEnabled
            veyra.nrLayers = saved.nrLayers; veyra.preferences = saved.preferences; veyra.liveStats = saved.liveStats; veyra.outputRateMode = saved.outputRateMode
            veyra.outputCustomFps = saved.outputCustomFps; veyra.displayRefreshHz = saved.displayRefreshHz; veyra.displaySync = saved.displaySync; veyra.vrrState = saved.vrrState
            veyra.captureSharperFormat = saved.sharper; veyra.captureAudioChoice = saved.audioChoice; veyra.captureAudioSuggestion = saved.audioSuggestion
            veyra.fgBackendChoices = saved.choices
        }
        function control(item, name) { const c = findChild(item, name); verify(c !== null, name); return c }
        function ids() { return diag.found.map(f => f.id) }
        function open() {
            host.dialog = "diagnose"
            tryVerify(function() { return diag.phase === "done" }, 3000)
        }

        function test_what_is_found_is_listed_with_its_change() {
            open()
            compare(ids(), ["enhance-small", "vfg-quality"])
            compare(diag.counts.act, 2)
            // The dialog fades in, and a row only reads visible once it does.
            tryVerify(function() { return findChild(diag, "pushfps-diagnose-item-enhance-small") !== null }, 3000)
            verify(control(diag, "pushfps-diagnose-title-enhance-small").text.indexOf("2160") >= 0, "the picture's own size is said")
            verify(control(diag, "pushfps-diagnose-body-vfg-quality").text.length > 0)
            verify(findChild(diag, "pushfps-diagnose-fix-enhance-small-0") !== null && findChild(diag, "pushfps-diagnose-fix-enhance-small-1") !== null,
                   "a 4K picture is offered full size and 1440p")
        }
        function test_nothing_changes_until_a_button_is_pressed_and_every_change_is_taken_back() {
            open()
            compare(Number(veyra.nrLayers[0].sizePolicy), 0, "opening the dialog changes nothing")
            compare(veyra.vfgQuality, 1)
            tryVerify(function() { return findChild(diag, "pushfps-diagnose-fix-enhance-small-0") !== null }, 3000)
            control(diag, "pushfps-diagnose-fix-enhance-small-0").clicked()
            compare(Number(veyra.nrLayers[0].sizePolicy), 1, "full size, on every enhancement layer")
            verify(diag.changed["enhance-small"] !== undefined)
            compare(ids(), ["enhance-small", "vfg-quality"], "the row stays so the change can be undone")
            tryVerify(function() { return control(diag, "pushfps-diagnose-undo-enhance-small").visible }, 3000)
            control(diag, "pushfps-diagnose-undo-enhance-small").clicked()
            compare(Number(veyra.nrLayers[0].sizePolicy), 0, "undo puts back what was set")
            verify(diag.changed["enhance-small"] === undefined)
            // NVIDIA VFG to High and back.
            control(diag, "pushfps-diagnose-fix-vfg-quality-0").clicked()
            compare(veyra.vfgQuality, 2)
            diag.undo(diag.found[1])
            compare(veyra.vfgQuality, 1)
        }
        function test_the_still_rule_is_switched_off_from_here() {
            const prefs = Object.assign({}, veyra.preferences); prefs.pushHudStill = true; veyra.preferences = prefs
            open()
            compare(ids()[0], "still-on")
            tryVerify(function() { return findChild(diag, "pushfps-diagnose-fix-still-on-0") !== null }, 3000)
            control(diag, "pushfps-diagnose-fix-still-on-0").clicked()
            compare(veyra.preferences.pushHudStill, false)
            diag.undo(diag.found[0])
            compare(veyra.preferences.pushHudStill, true)
        }
        function test_a_session_with_nothing_wrong_says_so() {
            veyra.vfgQuality = 2
            veyra.nrLayers = [{ index: 1, sizePolicy: 1, runtime: 2 }]
            open()
            compare(ids(), [])
            tryVerify(function() { return control(diag, "pushfps-diagnose-checked").visible }, 3000)
            compare(diag.counts.act, 0)
        }
        function test_nothing_is_judged_while_nothing_plays() {
            veyra.hasSource = false
            open()
            compare(ids(), [])
            verify(!control(diag, "pushfps-diagnose-checked").visible, "and it does not claim to have checked anything")
            verify(control(diag, "pushfps-diagnose-state").text.length > 0)
        }
        function test_the_card_that_is_behind_and_the_capture_card() {
            veyra.vfgQuality = 2
            veyra.nrLayers = [{ index: 1, sizePolicy: 1, runtime: 2 }]
            veyra.liveStats = ({ running: true, contentFps: 30, realFps: 30, generatedFps: 45, fgIdle: 0, fgMultiplierNow: 4 })
            veyra.captureSharperFormat = [{ fps: 30, pixel: "YUY2" }]
            veyra.captureAudioChoice = -1; veyra.captureAudioSuggestion = 3
            open()
            compare(ids(), ["fg-behind", "capture-compressed", "no-sound"])
            tryVerify(function() { return findChild(diag, "pushfps-diagnose-title-fg-behind") !== null }, 3000)
            verify(control(diag, "pushfps-diagnose-title-fg-behind").text.indexOf("75") >= 0 && control(diag, "pushfps-diagnose-title-fg-behind").text.indexOf("120") >= 0)
            control(diag, "pushfps-diagnose-fix-no-sound-0").clicked()
            compare(root.navigated, "capture", "the capture dialog has the one-tap choice")
        }
        function test_checking_again_reads_again() {
            open()
            compare(ids().length, 2)
            veyra.vfgQuality = 2
            diag.actionTriggered(diag.actions[1].label)
            tryVerify(function() { return diag.phase === "done" && diag.found.length === 1 }, 3000)
            compare(ids(), ["enhance-small"])
            diag.actionTriggered(diag.actions[0].label)
            compare(host.dialog, "", "the other button closes it")
        }
        function test_the_figures_are_a_mean_of_several_readings() {
            diag.sampleCount = 3
            host.dialog = "diagnose"
            tryVerify(function() { return diag.phase === "done" }, 3000)
            compare(diag.samples.length, 3)
            diag.sampleCount = 1
        }

        // --- quick setup -----------------------------------------------------------------
        function test_the_tile_opens_the_check() {
            control(quick, "pushfps-diagnose").clicked()
            compare(root.navigated, "diagnose")
        }
        function test_vfg_quality_is_chosen_in_quick_setup() {
            const row = control(quick, "pushfps-vfg-quality-row"), pick = control(quick, "pushfps-vfg-quality")
            verify(row.visible, "NVIDIA VFG is the method: its quality is offered")
            compare(pick.current, "1")
            pick.picked("2")
            compare(veyra.vfgQuality, 2)
            veyra.vfgQuality = 4
            compare(pick.current, "", "a mixed tier from Professional mode shows as none chosen")
            verify(row.hint.length > 0)
            veyra.fgBackendName = "dlss"
            verify(!row.visible, "another method has no such choice")
        }
    }
}
