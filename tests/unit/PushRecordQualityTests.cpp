// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The recorder's standard bitrate and its scale (engine/PushRecordQuality.h): at 100 % every size
// and rate gives the number the recorder gave before 4.0, a smaller share gives smaller files, and
// no share passes the recorder's limit. Pure: builds anywhere.
#include "veyra/engine/PushRecordQuality.h"
#include <cstdio>
#include <string>
using namespace veyra::engine;
static int failures = 0;
static void check(bool ok, const std::string& label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str()); if (!ok) ++failures; }
namespace before {
// The rule as LiveRecorder.cpp had it up to 3.2.10, kept here to compare against.
uint32_t autoBitrateMbps(uint32_t width, uint32_t height, uint32_t fps, bool hevc) {
    const double rate = std::min(fps, 60u) + std::max(int(fps) - 60, 0) * 0.5;
    const double mbps = double(width) * height * rate * 0.3 * (hevc ? 0.65 : 1.0) / 1e6;
    return uint32_t(std::clamp<long>(std::lround(mbps), hevc ? 8 : 10, 160));
}
}
int main() {
    const uint32_t sizes[][2] = {{3840, 2160}, {2560, 1440}, {1920, 1080}, {1280, 720}, {1080, 1920}, {1440, 2560}, {2160, 3840}, {3440, 1440}, {640, 360}, {7680, 4320}};
    const uint32_t rates[] = {10, 24, 30, 40, 50, 60, 90, 120, 144, 180, 240, 480};
    bool same = true;
    for (const auto& s : sizes) for (uint32_t fps : rates) for (int hevc = 0; hevc < 2; ++hevc)
        same = same && pushRecordAutoMbps(s[0], s[1], fps, hevc != 0) == before::autoBitrateMbps(s[0], s[1], fps, hevc != 0) &&
               pushRecordAutoMbps(s[0], s[1], fps, hevc != 0, 100) == before::autoBitrateMbps(s[0], s[1], fps, hevc != 0);
    check(same, "at 100 % every size, rate and codec gives the number the recorder always gave");
    check(pushRecordAutoMbps(1920, 1080, 60, false) == 37 && pushRecordAutoMbps(2560, 1440, 60, false) == 66 && pushRecordAutoMbps(3840, 2160, 60, false) == 149 &&
          pushRecordAutoMbps(1920, 1080, 120, false) == 56, "the standard: 1080p60 37, 1440p60 66, 4K60 149, 1080p120 56 Mbps");
    check(pushRecordAutoMbps(3840, 2160, 60, false, 50) == 75 && pushRecordAutoMbps(1920, 1080, 60, false, 50) == 19, "half: 4K60 75, 1080p60 19 Mbps");
    bool bounded = true, ordered = true;
    for (const auto& s : sizes) for (uint32_t fps : rates) for (int hevc = 0; hevc < 2; ++hevc) {
        uint32_t last = 0;
        for (uint32_t scale : {0u, 25u, 50u, 75u, 100u, 150u, 200u, 1000u}) {
            const uint32_t v = pushRecordAutoMbps(s[0], s[1], fps, hevc != 0, scale);
            bounded = bounded && v >= (hevc ? 4u : 5u) && v <= kPushRecordMaxMbps;
            ordered = ordered && v >= last; last = v;
        }
    }
    check(bounded, "no share leaves the recorder's range (at least 5 Mbps, 4 for HEVC; at most 160)");
    check(ordered, "a larger share never gives a lower rate");
    check(pushRecordAutoMbps(3840, 2160, 60, false, 0) == pushRecordAutoMbps(3840, 2160, 60, false, 25) &&
          pushRecordAutoMbps(3840, 2160, 60, false, 9999) == pushRecordAutoMbps(3840, 2160, 60, false, 200), "a share outside 25 to 200 % counts as the nearest end");
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
