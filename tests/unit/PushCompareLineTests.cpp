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
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
