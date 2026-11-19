// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFlow's ghost guard row (PushFlowGuardRow.qml, Professional mode): off unless a level is chosen,
// a pick writes the preference as a number, a stored value that cannot be is read as a level that
// can, and the share of generated frames it changed is shown only while PushFlow runs with the guard.
import QtQuick
import QtTest
import Veyra
Item {
    id: root
    width: 760; height: 420
    PushFlowGuardRow { id: guard; width: 640; y: 20 }

    TestCase {
        name: "PushFlowGuard"
        when: windowShown
        function init() {
            veyra.preferences = ({})
            veyra.hasSource = false
            veyra.liveStats = ({ running: false })
            guard.share = -1
        }
        function select() { return findChild(guard, "list-pushflow-guard") }
        function timer() { return findChild(guard, "list-pushflow-guard-timer") }

        function test_off_unless_a_level_is_chosen() {
            verify(select())
            compare(guard.level, 0)
            compare(select().value, select().options[0].label)
            verify(guard.hint.length > 0, "the row says that off changes nothing")
            verify(!timer().running, "nothing is read while the guard is off")
            compare(veyra.preferences.pushFlowGuard, undefined, "looking at the row writes nothing")
        }
        function test_a_pick_writes_the_level_as_a_number() {
            for (let level = 3; level >= 0; --level) {
                select().picked(String(level))
                verify(veyra.preferences.pushFlowGuard === level, "level " + level + " is stored as a number")
                compare(guard.level, level)
                compare(select().value, select().options[level].label)
            }
        }
        function test_a_stored_value_that_cannot_be_is_read_as_one_that_can() {
            veyra.preferences = ({ pushFlowGuard: 9 });      compare(guard.level, 3)
            veyra.preferences = ({ pushFlowGuard: -4 });     compare(guard.level, 0)
            veyra.preferences = ({ pushFlowGuard: "abc" });  compare(guard.level, 0)
            veyra.preferences = ({ pushFlowGuard: 1.6 });    compare(guard.level, 2)
            veyra.preferences = ({ pushFlowGuard: "2" });    compare(guard.level, 2)
            compare(select().value, select().options[2].label)
        }
        function test_the_share_is_shown_only_while_pushflow_runs_with_the_guard() {
            veyra.preferences = ({ pushFlowGuard: 2 })
            const idle = guard.hint
            veyra.hasSource = true
            tryVerify(() => timer().running, 2000, "it reads while something plays and a level is chosen")
            // Nothing generated yet: no number.
            guard.readShare()
            compare(guard.share, -1)
            compare(guard.hint, idle)
            // PushFlow runs with the guard: the share, as a percentage.
            veyra.liveStats = ({ running: true, pushFlowGuard: 2, pushFlowGuardShare: 0.034 })
            guard.readShare()
            fuzzyCompare(guard.share, 0.034, 1e-6)
            verify(guard.hint.indexOf("3.4") >= 0, "a small share keeps one decimal: " + guard.hint)
            veyra.liveStats = ({ running: true, pushFlowGuard: 2, pushFlowGuardShare: 0.42 })
            guard.readShare()
            verify(guard.hint.indexOf("42") >= 0 && guard.hint.indexOf("42.0") < 0, "a large one is a whole number: " + guard.hint)
            // A number that cannot be a share is not shown as one.
            veyra.liveStats = ({ running: true, pushFlowGuard: 2, pushFlowGuardShare: 7 })
            guard.readShare()
            compare(guard.share, 1)
            // Another method is in use, or playback stopped: no number.
            veyra.liveStats = ({ running: true, pushFlowGuard: 0, pushFlowGuardShare: 0.5 })
            guard.readShare()
            compare(guard.share, -1)
            veyra.liveStats = ({ running: false, pushFlowGuard: 2, pushFlowGuardShare: 0.5 })
            guard.readShare()
            compare(guard.share, -1)
        }
        function test_turning_it_off_forgets_the_share() {
            veyra.preferences = ({ pushFlowGuard: 3 })
            veyra.hasSource = true
            veyra.liveStats = ({ running: true, pushFlowGuard: 3, pushFlowGuardShare: 0.2 })
            tryVerify(() => timer().running, 2000)
            tryVerify(() => guard.share > 0.19, 6000, "the timer reads the share by itself")
            select().picked("0")
            tryCompare(guard, "share", -1, 2000)
            verify(!timer().running)
        }
    }
}
