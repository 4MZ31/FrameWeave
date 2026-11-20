// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PS5 Remote Play's sound sync (remoteplay/PresentedArrivals.h with sink/CaptureSyncTarget.h): a sound
// time line that runs late must not hold the sound back.
#include "veyra/remoteplay/PresentedArrivals.h"
#include "veyra/sink/CaptureSyncTarget.h"

#include <cstdio>
#include <string>

using veyra::remoteplay::PresentedArrivals;
using veyra::sink::CaptureSyncTarget;

static int failures = 0;
static void check(bool ok, const std::string& label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str()); if (!ok) ++failures; }

int main() {
    {   // The list of arrivals.
        PresentedArrivals list;
        check(!list.find(0) && list.size() == 0, "nothing is found in an empty list");
        list.note(1000000, 5000000);
        list.note(1166667, 5170000);
        check(list.find(1000000) == 5000000 && list.find(1166667) == 5170000, "a frame is found by its time stamp");
        check(list.find(1000003) == 5000000 && list.find(999996) == 5000000, "a stamp that went through milliseconds and back is the same frame");
        check(!list.find(1083333), "a generated frame's stamp, between two real ones, is no frame that arrived");
        check(!list.find(1000006) && !list.find(999994), "a stamp six ticks off is another one");
        list.note(2000000, 0); list.note(2000000, -5);
        check(!list.find(2000000), "an arrival that is no time is not kept");
        list.note(1000000, 7000000);
        check(list.find(1000000) == 7000000, "the newest frame with a stamp wins (a reconnect starts the stamps again)");
        for (int i = 0; i < 200; ++i) list.note(10000000 + i * 166667, 20000000 + i * 166667);
        check(list.size() == PresentedArrivals::kKept, "the list keeps its last frames only");
        check(!list.find(1000000) && !list.find(10000000), "an old frame is forgotten");
        check(list.find(10000000 + 199 * 166667) == 20000000 + 199 * 166667 && list.find(10000000 + 136 * 166667).has_value(), "the last 64 are there");
        list.clear();
        check(list.size() == 0 && !list.find(10000000 + 199 * 166667), "cleared");
    }
    // The fault and the fix, as the sound session works it out. Times in milliseconds on one clock.
    // The picture takes 90 ms from arrival to screen. The sound's time line runs 1900 ms late (its
    // first block came that much late), so "picture delay by the stamps" reads 90 + 1900.
    const double pictureMs = 90, lateMs = 1900;
    {   // Before 4.1: no arrival is given, and the stamps are believed whatever they say.
        CaptureSyncTarget target;
        CaptureSyncTarget::Result r{};
        for (int i = 0; i < 30; ++i) r = target.observe(pictureMs + lateMs, 100000.0 + i * 16.7, std::nullopt);
        check(r.targetMs > 1500, "without the arrival the sound is held back by the whole error (" + std::to_string(r.targetMs) + " ms asked, 1500 after the session's limit)");
    }
    {   // 4.1: with the shown frame's arrival, stamps that do not fit are not believed.
        CaptureSyncTarget target;
        CaptureSyncTarget::Result r{};
        for (int i = 0; i < 30; ++i) { const double host = 100000.0 + i * 16.7; r = target.observe(pictureMs + lateMs, host, host - pictureMs); }
        check(r.fallback && r.targetMs > pictureMs - 1 && r.targetMs < pictureMs + 1, "with the arrival the sound waits as long as the picture really takes (" + std::to_string(r.targetMs) + " ms)");
    }
    {   // Time lines that fit are used as before: nothing changes for a session that was in sync.
        CaptureSyncTarget target;
        CaptureSyncTarget::Result r{};
        for (int i = 0; i < 30; ++i) { const double host = 100000.0 + i * 16.7; r = target.observe(pictureMs + 12, host, host - pictureMs); }
        check(!r.fallback && r.targetMs > pictureMs + 11 && r.targetMs < pictureMs + 13, "stamps within the bound are believed, as before");
    }
    {   // The picture's time line running late (frames stamped by a nominal rate the stream does not keep).
        CaptureSyncTarget target;
        CaptureSyncTarget::Result r{};
        for (int i = 0; i < 30; ++i) { const double host = 100000.0 + i * 16.7; r = target.observe(pictureMs + 400 + i * 8.0, host, host - pictureMs); }
        check(r.fallback && r.targetMs < pictureMs + 1, "a picture time line that falls behind does not pull the sound after it");
    }
    std::printf(failures ? "%d FAILED\n" : "ALL PASSED (%d failures)\n", failures);
    return failures ? 1 : 0;
}
