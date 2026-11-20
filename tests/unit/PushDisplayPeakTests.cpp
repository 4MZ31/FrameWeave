// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The peak brightness a display reports (gfx/PushDisplayPeak.h): when may it scale the HDR tone
// curve? A measurement may; the top of the signal's range, or a report that contradicts itself,
// leaves the curve at its reference. A peak the user set always wins. Pure: builds anywhere.
#include "veyra/gfx/PushDisplayPeak.h"
#include <cstdio>
#include <limits>
#include <string>
using namespace veyra::gfx;
static int failures = 0;
static void check(bool ok, const std::string& label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str()); if (!ok) ++failures; }
int main() {
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    // --- reports that are measurements --------------------------------------------------------------
    struct Panel { const char* what; double peak, full, black; };
    for (const Panel& p : { Panel{"an HDR400 LCD", 400, 350, 0.4}, Panel{"an HDR600 LCD", 603, 480, 0.05}, Panel{"an OLED monitor, black 0", 1000, 250, 0.0},
                            Panel{"an OLED TV, black 0.0001", 800, 180, 0.0001}, Panel{"a 1500 nit mini-LED", 1500, 700, 0.002}, Panel{"a 2000 nit mini-LED", 2000, 900, 0.0},
                            Panel{"whole screen as bright as the peak", 600, 600, 0.1}, Panel{"whole screen unknown", 1000, 0, 0.02},
                            Panel{"a 4000 nit panel with a real black", 4000, 1000, 0.02}, Panel{"3999 nits, black 0", 3999, 0, 0.0} })
        check(judgeDisplayPeak(p.peak, p.full, p.black) == DisplayPeakTrust::Trusted, std::string("trusted: ") + p.what);
    // A black of nothing alone never counts against a report.
    for (double peak : {100.0, 400.0, 1000.0, 2500.0, 3999.0})
        check(judgeDisplayPeak(peak, 0, 0.0) == DisplayPeakTrust::Trusted, "black 0 is trusted at " + std::to_string(int(peak)) + " nits");
    // --- the signal's ceiling -----------------------------------------------------------------------
    check(judgeDisplayPeak(10000, 10000, 0.0) == DisplayPeakTrust::Ceiling, "10000 / 0: the report that started this");
    check(judgeDisplayPeak(10000, 500, 0.05) == DisplayPeakTrust::Ceiling, "10000 nits is the top of the range whatever the black says");
    check(judgeDisplayPeak(4000, 0, 0.0) == DisplayPeakTrust::Ceiling && judgeDisplayPeak(5000, 2000, 0.005) == DisplayPeakTrust::Ceiling, "very high with an idealised black");
    check(judgeDisplayPeak(4000, 0, 0.0099) == DisplayPeakTrust::Ceiling && judgeDisplayPeak(4000, 0, 0.01) == DisplayPeakTrust::Trusted, "the black's line is 0.01");
    check(judgeDisplayPeak(9999, 0, 0.05) == DisplayPeakTrust::Trusted, "just under the range's top, with a real black, is believed");
    // --- reports that contradict themselves ---------------------------------------------------------
    check(judgeDisplayPeak(600, 900, 0.1) == DisplayPeakTrust::Contradictory, "the whole screen brighter than a small area");
    check(judgeDisplayPeak(600, 601, 0.1) == DisplayPeakTrust::Trusted && judgeDisplayPeak(600, 601.5, 0.1) == DisplayPeakTrust::Contradictory, "one nit of rounding is allowed");
    check(judgeDisplayPeak(300, 0, 300) == DisplayPeakTrust::Contradictory && judgeDisplayPeak(300, 0, 450) == DisplayPeakTrust::Contradictory, "a black as bright as the peak");
    // --- nothing usable ----------------------------------------------------------------------------
    for (double peak : {nan, inf, -inf, -100.0, 0.0, 99.9, 10000.5, 1e9})
        check(judgeDisplayPeak(peak, 100, 0.1) == DisplayPeakTrust::Unknown, "no usable peak");
    check(judgeDisplayPeak(800, nan, nan) == DisplayPeakTrust::Trusted && judgeDisplayPeak(800, -5, -1) == DisplayPeakTrust::Trusted && judgeDisplayPeak(800, inf, 0.1) == DisplayPeakTrust::Trusted,
          "a broken whole-screen or black value is taken as unknown, not as a contradiction");
    // --- what the curve gets ------------------------------------------------------------------------
    check(displayPeakForCurve(0, 800, true) == 800, "a trusted report scales the curve");
    check(displayPeakForCurve(0, 10000, false) == 0, "a report that is not trusted leaves the curve at its reference");
    check(displayPeakForCurve(0, 0, false) == 0, "and so does no report");
    check(displayPeakForCurve(600, 10000, false) == 600 && displayPeakForCurve(600, 1500, true) == 600 && displayPeakForCurve(10000, 800, true) == 10000, "a peak set by hand always wins, 10000 too");
    check(displayPeakForCurve(0, 50, true) == 100 && displayPeakForCurve(0, 20000, true) == 10000, "a trusted report stays inside the curve's range");
    check(std::string(displayPeakTrustName(DisplayPeakTrust::Ceiling)) == "ceiling" && std::string(displayPeakTrustName(DisplayPeakTrust::Unknown)) == "unknown", "names for the log");
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
