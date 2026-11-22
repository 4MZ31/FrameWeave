// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The line of the live split comparison (engine/PushCompareLine.h): centred on the split, a white
// core between two dark edges, thick enough to see at every size, inside the picture, and absent
// when one side has nothing to show. Pure: builds anywhere.
#include "veyra/engine/PushCompareLine.h"
#include <cstdio>
#include <string>
using namespace veyra::engine;
static int failures = 0;
static void check(bool ok, const std::string& label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str()); if (!ok) ++failures; }
int main() {
    check(pushCompareLineCore(720) == 2 && pushCompareLineCore(1080) == 3 && pushCompareLineCore(1440) == 4 && pushCompareLineCore(2160) == 6, "core: 2, 3, 4, 6 pixels at 720, 1080, 1440, 2160");
    check(pushCompareLineEdge(720) == 1 && pushCompareLineEdge(1080) == 1 && pushCompareLineEdge(1440) == 2 && pushCompareLineEdge(2160) == 3, "edges: 1, 1, 2, 3 pixels");
    check(pushCompareLineCore(1) == 2 && pushCompareLineCore(0) == 2 && pushCompareLineEdge(1) == 1, "never thinner than 2 + 1 + 1, whatever the buffer");
    {
        const auto l = pushCompareLine(960, 0, 1920, 1080);
        check(l.visible && l.coreLeft == 959 && l.coreRight == 962 && l.outerLeft == 958 && l.outerRight == 963, "1080 high, split in the middle: core 959..962, edges one pixel each side");
        check(l.coreLeft <= 960 && l.coreRight > 960, "the core covers the place where the sides meet");
    }
    {
        const auto l = pushCompareLine(1920, 0, 3840, 2160);
        check(l.visible && l.coreRight - l.coreLeft == 6 && l.outerRight - l.outerLeft == 12 && l.coreLeft - l.outerLeft == 3, "2160 high: 6 pixels of white, 3 of dark each side");
    }
    {   // A picture with bars beside it (or cropped): the line stays inside the picture.
        const auto nearLeft = pushCompareLine(241, 240, 1680, 1080), nearRight = pushCompareLine(1679, 240, 1680, 1080);
        check(nearLeft.visible && nearLeft.outerLeft >= 240 && nearLeft.coreLeft >= 240, "a split one pixel in from the left edge: nothing drawn outside the picture");
        check(nearRight.visible && nearRight.outerRight <= 1680 && nearRight.coreRight <= 1680, "and the same at the right edge");
    }
    {   // Nothing to tell apart: no line.
        check(!pushCompareLine(0, 0, 1920, 1080).visible, "split at the left edge (all PushFPS): no line");
        check(!pushCompareLine(1920, 0, 1920, 1080).visible, "split at the right edge (all original): no line");
        check(!pushCompareLine(240, 240, 1680, 1080).visible && !pushCompareLine(1680, 240, 1680, 1080).visible, "the same for a picture that does not fill the buffer");
        check(!pushCompareLine(100, 500, 400, 1080).visible && !pushCompareLine(900, 500, 400, 1080).visible, "a split outside the picture, or no picture at all");
    }
    {   // Every position across a picture: the white is always whole or cut by the picture's edge only.
        bool whole = true;
        for (long x = 1; x < 1280; ++x) {
            const auto l = pushCompareLine(x, 0, 1280, 720);
            whole = whole && l.visible && l.outerLeft <= l.coreLeft && l.coreLeft < l.coreRight && l.coreRight <= l.outerRight && l.outerLeft >= 0 && l.outerRight <= 1280;
        }
        check(whole, "every split position gives a line inside the picture, edges outside the core");
    }
    check(kPushCompareWhitePq > 0.57f && kPushCompareWhitePq < 0.59f && kPushCompareWhiteScRgb > 2.5f && kPushCompareWhiteScRgb < 2.6f, "on an HDR output the line is reference white (203 nits), not the peak");
    // 4.2.1: which side the reference is on.
    {
        // Not swapped: the bounds the presenter always set (left stays, right = max(left, min(right, split))).
        bool same = true, inside = true, shared = true, whole = true;
        for (long left : {0L, 213L, 640L}) for (long right : {1280L, 1067L, 640L}) for (long split = -50; split <= 1400; split += 7) {
            if (right < left) continue;
            const PushCompareSides a = pushCompareSides(left, right, split, false), b = pushCompareSides(left, right, split, true);
            same = same && a.referenceLeft == left && a.referenceRight == std::max(left, std::min(right, split)) && a.line == a.referenceRight;
            inside = inside && a.referenceLeft >= left && a.referenceRight <= right && a.referenceLeft <= a.referenceRight
                            && b.referenceLeft >= left && b.referenceRight <= right && b.referenceLeft <= b.referenceRight;
            // The two choices share the line and together cover the part exactly once.
            shared = shared && a.line == b.line && b.referenceLeft == a.referenceRight && b.referenceRight == right && b.line == b.referenceLeft;
            whole = whole && (a.referenceRight - a.referenceLeft) + (b.referenceRight - b.referenceLeft) == right - left;
        }
        check(same, "sides: not swapped, the reference is the left part with the bounds it always had");
        check(inside, "sides: either way the reference lies inside the picture's part and is never inside out");
        check(shared && whole, "sides: swapped, the reference is the right part, from the same line, and the two parts make the whole");
        const PushCompareSides l = pushCompareSides(0, 1920, 480, false), r = pushCompareSides(0, 1920, 480, true);
        check(l.referenceLeft == 0 && l.referenceRight == 480 && l.line == 480 && r.referenceLeft == 480 && r.referenceRight == 1920 && r.line == 480,
              "sides: a split at a quarter: 0..480 before on the left, or 480..1920 before on the right");
        check(!pushCompareSwapped(), "sides: not swapped unless asked");
        setPushCompareSwapped(true); const bool on = pushCompareSwapped(); setPushCompareSwapped(false);
        check(on && !pushCompareSwapped(), "sides: the switch is kept and given back");
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
