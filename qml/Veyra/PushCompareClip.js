// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS comparison clips: how long one runs. Shared by the dialog (PushCompareDialog.qml), Settings,
// the quick setup tile, Streaming mode and the video playback page, so they can never disagree.
//
// Preferences: pushCompareSeconds (one of `lengths`; anything else counts as 15) and
// pushCompareUntilStop (no timer: the clip runs until it is stopped, in every layout).
.pragma library

var lengths = [10, 15, 30, 60, 90, 120]

function seconds(prefs) {
    var n = Number((prefs && prefs.pushCompareSeconds) || 15)
    return lengths.indexOf(n) >= 0 ? n : 15
}

function untilStop(prefs) { return !!prefs && prefs.pushCompareUntilStop === true }

// What pushStartCompareClip takes: 0 records until stopped.
function startSeconds(prefs) { return untilStop(prefs) ? 0 : seconds(prefs) }

// A running clip's time as 01:23 (1:02:03 from an hour on).
function elapsed(total) {
    var s = Math.max(0, Math.floor(Number(total) || 0))
    var h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60, sec = s % 60
    function two(n) { return (n < 10 ? "0" : "") + n }
    return h > 0 ? h + ":" + two(m) + ":" + two(sec) : two(m) + ":" + two(sec)
}
