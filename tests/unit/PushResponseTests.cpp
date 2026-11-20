// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFlow 2's Response, first step (engine/PushResponse.h): does the picture's motion follow the stick,
// how strongly and how late? Made-up sticks and pictures with a known answer. Pure: also builds with g++.
#include "veyra/engine/PushResponse.h"
#include <cstdio>

using namespace veyra::engine::response;

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* what) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", what); failures += !ok; };
    const int64_t ms = 10000;
    // A stick that sweeps slowly left and right and up and down, polled every 4 ms; a picture at 60 frames a
    // second that turns `gain` widths a second at full stick, `delay` ms after the stick, plus `noise`.
    const auto play = [&](Observer& o, float gainX, float gainY, int delayMs, float noise, int seconds, bool stickMoves = true) {
        const auto stickAt = [&](int64_t t, float& x, float& y) {
            const double s = double(t) / 1e7;
            x = stickMoves ? float(std::sin(s * 1.7) * 0.9) : 0.0f; y = stickMoves ? float(std::sin(s * 0.9 + 1.0) * 0.6) : 0.0f;
        };
        uint32_t seed = 12345;
        const auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return float(int32_t(seed >> 8) % 2001 - 1000) / 1000.0f; };
        for (int64_t t = 0; t < int64_t(seconds) * 1000 * ms; t += 4 * ms) { float x, y; stickAt(t, x, y); o.stick(1000000 + t, x, y); }
        for (int64_t t = 500 * ms; t < int64_t(seconds) * 1000 * ms; t += 166667) {
            float x, y; stickAt(t - int64_t(delayMs) * ms, x, y);
            o.motion(1000000 + t, gainX * x + noise * rnd(), gainY * y + noise * rnd());
        }
    };
    {
        Observer o;
        play(o, 0.5f, 0.3f, 60, 0.0f, 10);
        check(o.fit().frames == 0 && o.sticksSeen() == 0, "switched off, nothing is kept");
        o.listen(true);
        play(o, -0.5f, 0.3f, 60, 0.02f, 12);
        const Fit f = o.fit();
        std::printf("INFO delay %.0f ms, gain %.3f / %.3f, explained %.2f, %zu frames with the stick held of %zu\n", f.delayMs, f.gainX, f.gainY, f.explained, f.active, f.frames);
        check(f.enough && f.follows, "a picture that turns with the stick is found to follow it");
        check(std::abs(f.delayMs - 60.0f) <= 10.0f, "and how late: 60 ms, to the 10 ms it is searched in");
        check(std::abs(f.gainX + 0.5f) < 0.03f && std::abs(f.gainY - 0.3f) < 0.03f, "and how strongly, with its direction, on each axis");
        check(f.explained > 0.9f, "the stick explains nearly all of that motion");
        o.listen(false);
        check(o.fit().frames == 0 && o.sticksSeen() == 0 && !o.listening(), "switched off: everything observed is gone at once");
    }
    {
        Observer o; o.listen(true);
        play(o, 0.0f, 0.0f, 0, 0.3f, 12);          // the picture moves on its own (a cutscene): nothing to do with the stick
        const Fit f = o.fit();
        check(f.enough && !f.follows && f.explained < 0.2f, "a picture that moves on its own is not said to follow the stick");
    }
    {
        Observer o; o.listen(true);
        play(o, 0.5f, 0.3f, 60, 0.02f, 12, false);  // nobody touches the stick
        const Fit f = o.fit();
        check(!f.enough && !f.follows && f.active == 0 && f.frames > 0, "a stick at rest gives nothing to judge, and nothing is claimed");
    }
    {
        Observer o; o.listen(true);
        play(o, 0.5f, 0.3f, 60, 0.02f, 1);          // a second of play: too little
        check(!o.fit().enough, "too little stick movement: nothing is claimed yet");
        o.clear();
        check(o.fit().frames == 0 && o.listening(), "a new source starts from nothing, still listening");
    }
    {
        Observer o; o.listen(true);
        // A controller on the console: PushFPS sees no stick at all, only the picture.
        for (int64_t t = 0; t < 10 * 1000 * ms; t += 166667) o.motion(1000000 + t, 0.3f, 0.0f);
        const Fit f = o.fit();
        check(!f.enough && !f.follows && o.sticksSeen() == 0, "no stick through PushFPS: nothing is claimed");
        o.stick(5, 9.0f, -9.0f); o.motion(6, std::nanf(""), 0.0f);
        check(o.sticksSeen() == 1, "a stick value out of range is kept in range, a motion that is not a number is dropped");
    }
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
