// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#pragma once
// A fixed output cap and the screen the window is on (PushFPS 4.1; reports of "moved PushFPS to my
// other monitor and it went back to 30 FPS").
//
// The quick setup's 120 FPS target is a fixed cap of 120. On a 120 Hz screen (or faster) that is what
// the screen shows. Moved to a 60 Hz screen with vertical sync on (smooth frame pacing, the default),
// every present waits for a refresh, so 60 pictures a second is all that can be handed over: frame
// generation went on making 120, its frames ran past their deadlines, the budget turned the pairs
// away, and what was left on screen was the game's own 30.
//
// The cap that follows the display never had this (it is the screen's refresh rate by definition), and
// the engine already lowers the multiplier to the smallest one that reaches the cap. So a fixed cap
// the screen cannot show is lowered to the screen's refresh rate while presents wait for it: a 30 FPS
// game then runs at 2X for 60 on the 60 Hz screen, and at its full target again back on the fast one.
//
// Only with vertical sync (explicit, or automatic with variable refresh, which presents the same
// way): without it a present replaces a frame not shown yet instead of waiting, nothing queues, and
// the choice stays the user's. An unknown refresh rate changes nothing.
namespace veyra::engine {

// How far above the refresh rate a cap may be and still count as fitting (the display budget's own
// tolerance, DisplayFgBudget.h: 60 FPS on a 59.94 Hz screen fits).
inline constexpr double kPushCapFitTolerance = 1.02;

// capFps: the fixed cap asked for. refreshHz: the screen's refresh rate, 0 when not known.
// waits: every present waits for a refresh. Returns the cap to apply.
constexpr double pushCapForDisplay(double capFps, double refreshHz, bool waits) {
    if (!waits) return capFps;
    // Written so that a rate that is not a number fails every test and leaves the cap alone.
    if (!(capFps > 0.0) || !(refreshHz >= 1.0) || !(refreshHz < 100000.0)) return capFps;
    return capFps > refreshHz * kPushCapFitTolerance ? refreshHz : capFps;
}

}  // namespace veyra::engine
