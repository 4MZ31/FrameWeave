// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Lip sync by hand (sink/PushAudioTrim.h): the trim is added to the automatic sound delay, and to
// nothing else. With no trim every mode asks for exactly the delay it asked for before 4.0.
#include "veyra/sink/PushAudioTrim.h"
#include <cstdio>
#include <string>
using namespace veyra::sink;
static int failures = 0;
static void check(bool ok, const std::string& label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str()); if (!ok) ++failures; }
// What the session asked for before 4.0, word for word.
static double before(unsigned mode, double automaticMs, int offsetMs) { return mode == 0 ? automaticMs : mode == 1 ? double(offsetMs) : 0; }
int main() {
    bool same = true;
    for (unsigned mode : {0u, 1u, 2u}) for (double automatic : {0.0, 8.3, 24.0, 41.7, 333.0}) for (int offset : {-250, -40, 0, 30, 250})
        same = same && pushAudioRequestedMs(mode, automatic, offset, 0) == before(mode, automatic, offset);
    check(same, "without a trim every mode asks for what it asked for before 4.0");
    check(pushAudioRequestedMs(0, 24.0, 0, 40) == 64.0, "automatic 24 ms, 40 ms later: 64");
    check(pushAudioRequestedMs(0, 24.0, 0, -10) == 14.0, "automatic 24 ms, 10 ms earlier: 14");
    check(pushAudioRequestedMs(0, 24.0, 0, -100) == -76.0, "further back than there is delay is asked for as it is (the session holds it at 0 and reports it limited)");
    check(pushAudioRequestedMs(1, 24.0, 60, 40) == 60.0 && pushAudioRequestedMs(1, 24.0, -30, -90) == -30.0, "manual mode is its own number: the trim does not touch it");
    check(pushAudioRequestedMs(2, 24.0, 60, 40) == 0.0, "off is no delay at all");
    setPushAudioTrimMs(35); check(pushAudioTrimMs() == 35, "the trim is kept");
    setPushAudioTrimMs(9000); check(pushAudioTrimMs() == kPushAudioTrimLimitMs, "never more than a quarter of a second later");
    setPushAudioTrimMs(-9000); check(pushAudioTrimMs() == -kPushAudioTrimLimitMs, "nor earlier");
    setPushAudioTrimMs(0); check(pushAudioTrimMs() == 0, "and back to none");
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
