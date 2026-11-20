// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#include "veyra/engine/PushDisplayCap.h"

#include <cmath>
#include <iostream>
#include <limits>

using veyra::engine::pushCapForDisplay;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { ++failures; std::cout << "FAIL " << what << "\n"; }
}
// The engine's own reduction (EngineController.cpp): the smallest multiplier that reaches the cap,
// never under 2 and never over what was asked.
unsigned multiplierFor(double cap, double contentFps, unsigned asked) {
    const auto needed = unsigned(std::ceil(cap / contentFps - 1e-6));
    return needed < 2u ? 2u : needed > asked ? asked : needed;
}
}  // namespace

int main() {
    // The report: a 120 FPS target, the window moved to a 60 Hz screen, vertical sync on.
    check(pushCapForDisplay(120, 60, true) == 60, "120 on a 60 Hz screen is 60");
    check(multiplierFor(pushCapForDisplay(120, 60, true), 30, 4) == 2, "a 30 FPS game then runs at 2X");
    // Back on the fast screen it is the target again.
    check(pushCapForDisplay(120, 144, true) == 120, "120 on a 144 Hz screen stays");
    check(pushCapForDisplay(120, 240, true) == 120, "120 on a 240 Hz screen stays");
    check(multiplierFor(pushCapForDisplay(120, 240, true), 30, 4) == 4, "and the game is at 4X again");
    check(pushCapForDisplay(120, 120, true) == 120, "a cap the screen shows exactly stays");
    // A television's 59.94 Hz and a 60 FPS cap: that fits, as the display budget says.
    check(pushCapForDisplay(60, 59.94, true) == 60, "60 on 59.94 Hz fits");
    check(pushCapForDisplay(120, 119.88, true) == 120, "120 on 119.88 Hz fits");
    check(pushCapForDisplay(90, 60, true) == 60, "90 on a 60 Hz screen is 60");
    check(pushCapForDisplay(120, 90, true) == 90, "120 on a 90 Hz screen is 90");
    check(multiplierFor(pushCapForDisplay(120, 90, true), 30, 4) == 3, "30 FPS on a 90 Hz screen: 3X");
    check(pushCapForDisplay(60, 144, true) == 60, "a cap under the refresh rate is never raised");
    check(pushCapForDisplay(60, 50, true) == 50, "60 on a 50 Hz screen is 50");
    // Without vertical sync nothing waits and nothing queues: the choice stays as it is.
    check(pushCapForDisplay(120, 60, false) == 120, "no vertical sync: unchanged");
    check(pushCapForDisplay(1000, 60, false) == 1000, "no vertical sync: unchanged, whatever the cap");
    // A refresh rate that is not known, or not a rate, changes nothing.
    check(pushCapForDisplay(120, 0, true) == 120, "unknown refresh rate: unchanged");
    check(pushCapForDisplay(120, 0.5, true) == 120, "a refresh rate under 1 Hz is not one");
    check(pushCapForDisplay(120, -60, true) == 120, "a negative refresh rate is not one");
    check(pushCapForDisplay(120, std::numeric_limits<double>::quiet_NaN(), true) == 120, "not a number: unchanged");
    check(pushCapForDisplay(120, std::numeric_limits<double>::infinity(), true) == 120, "infinite: unchanged");
    check(pushCapForDisplay(0, 60, true) == 0, "no cap stays no cap");
    check(pushCapForDisplay(-5, 60, true) == -5, "a cap that is not one is left for the caller");
    check(std::isnan(pushCapForDisplay(std::numeric_limits<double>::quiet_NaN(), 60, true)), "a cap that is not a number is left for the caller");
    // It is a constant expression (used where the presenter works out its settings).
    static_assert(pushCapForDisplay(120, 60, true) == 60);
    static_assert(pushCapForDisplay(120, 60, false) == 120);
    if (failures == 0) std::cout << "veyra_push_display_cap_tests OK\n";
    return failures == 0 ? 0 : 1;
}
