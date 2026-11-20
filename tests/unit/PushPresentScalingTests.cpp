// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// How the picture is resized to the window (engine/PushPresentScaling.h). Pure: also builds with g++.
#include "veyra/engine/PushPresentScaling.h"
#include <cstdio>
using namespace veyra::engine;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::printf("FAILED line %d: %s\n", __LINE__, #x); } } while (0)
int main() {
    // On unless switched off: the fine path wherever the graph allows it.
    CHECK(pushSharpScaling());
    CHECK(pushFinePresentation(true, false));
    CHECK(pushFinePresentation(true, true));
    CHECK(!pushFinePresentation(false, false));
    CHECK(!pushFinePresentation(false, true));   // a test variable forbids it: nothing brings it back
    // Switched off: exactly what every version before 4.1 drew, the fine path only where it always was.
    setPushSharpScaling(false);
    CHECK(!pushSharpScaling());
    CHECK(!pushFinePresentation(true, false));   // a capture card, a file, a screen: the plain sample again
    CHECK(pushFinePresentation(true, true));     // PS5 Remote Play with reconstructed chroma keeps its fine path
    CHECK(!pushFinePresentation(false, true));
    setPushSharpScaling(true);
    CHECK(pushFinePresentation(true, false));
    // The rule itself, for every combination.
    static_assert(pushFinePresentation(true, false, true) && pushFinePresentation(true, true, false));
    static_assert(!pushFinePresentation(true, false, false) && !pushFinePresentation(false, true, true));
    if (failures) { std::printf("%d failed\n", failures); return 1; }
    std::printf("PushPresentScalingTests: all passed\n");
    return 0;
}
