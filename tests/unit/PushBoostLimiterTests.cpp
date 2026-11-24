// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Volume above 100 %: loud peaks are turned down and keep their shape, where they were cut off flat.
// Builds anywhere: g++ -std=c++20 -Iinclude tests/unit/PushBoostLimiterTests.cpp
#include "veyra/sink/AudioGain.h"
#include "veyra/sink/PushBoostLimiter.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace veyra::sink;

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// A 440 Hz tone, stereo, at the renderer's rate.
static std::vector<float> tone(double seconds, float level, unsigned channels = 2) {
    const std::size_t frames = std::size_t(seconds * 48000);
    std::vector<float> out(frames * channels);
    for (std::size_t i = 0; i < frames; ++i)
        for (unsigned c = 0; c < channels; ++c) out[i * channels + c] = level * float(std::sin(2.0 * 3.14159265358979 * 440.0 * double(i) / 48000.0));
    return out;
}
static float peakOf(const std::vector<float>& v, std::size_t from = 0) {
    float p = 0; for (std::size_t i = from; i < v.size(); ++i) p = std::max(p, std::abs(v[i])); return p;
}
// How far a tone is from a pure one: the share of its energy that is not at 440 Hz (what clipping adds).
static double distortion(const std::vector<float>& v, unsigned channels, std::size_t fromFrame) {
    double s = 0, c = 0, total = 0; std::size_t n = 0;
    for (std::size_t i = fromFrame; i * channels < v.size(); ++i, ++n) {
        const double x = v[i * channels], a = 2.0 * 3.14159265358979 * 440.0 * double(i) / 48000.0;
        s += x * std::sin(a); c += x * std::cos(a); total += x * x;
    }
    const double fundamental = 2.0 * (s * s + c * c) / double(n);
    return total > 0 ? std::max(0.0, 1.0 - fundamental / total) : 0.0;
}

int main() {
    {   // quiet sound that is boosted but stays under full scale is not touched
        auto v = tone(0.25, 0.6f); const auto before = v;
        PushBoostLimiter limiter; limiter.process(v.data(), v.size() / 2, 2);
        check(v == before, "sound under the ceiling passes bit for bit");
        check(limiter.gain == 1.0f, "and nothing is turned down");
    }
    {   // a tone at three times full scale (300 % of a full-scale game)
        auto clipped = tone(0.5, 3.0f); auto limited = clipped;
        limitPcmPeaks(clipped.data(), clipped.size() / 2, 2);
        PushBoostLimiter limiter; limiter.process(limited.data(), limited.size() / 2, 2);
        check(peakOf(limited) <= PushBoostLimiter::kCeiling + 1e-6f, "nothing passes the ceiling");
        const double was = distortion(clipped, 2, 4800), now = distortion(limited, 2, 4800);
        std::printf("     a full-scale tone at 300 %%: %.1f %% of the energy was distortion, now %.3f %%\n", was * 100, now * 100);
        check(was > 0.05, "cut off flat, over a twentieth of it was distortion (what was heard as crackling)");
        check(now < 0.001, "turned down instead, under a thousandth is");
        check(std::abs(peakOf(limited, limited.size() / 2) - PushBoostLimiter::kCeiling) < 0.02f, "and it plays as loud as it can");
    }
    {   // one loud hit in quiet sound: down at once, back within a fraction of a second
        auto v = tone(1.0, 0.3f); v[2 * 4800] = 2.5f; v[2 * 4800 + 1] = 2.5f;
        PushBoostLimiter limiter; limiter.process(v.data(), v.size() / 2, 2);
        check(std::abs(v[2 * 4800]) <= PushBoostLimiter::kCeiling + 1e-6f, "the hit itself is held at the ceiling");
        check(limiter.gain > 0.99f, "and 0.9 s later the volume is back");
        auto early = tone(1.0, 0.3f); early[2 * 4800] = 2.5f; early[2 * 4800 + 1] = 2.5f;
        PushBoostLimiter second; second.process(early.data(), 4800 + 3840, 2);
        check(second.gain > 0.70f && second.gain < 0.85f, "80 ms after the hit it is about two thirds of the way back");
    }
    {   // both channels move together
        std::vector<float> v(2 * 4800, 0.0f);
        for (std::size_t i = 0; i < 4800; ++i) { v[2 * i] = 2.0f; v[2 * i + 1] = 0.5f; }
        PushBoostLimiter limiter; limiter.process(v.data(), 4800, 2);
        check(std::abs(v[2 * 100] / v[2 * 100 + 1] - 4.0f) < 1e-4f, "left and right keep their balance");
    }
    {   // in pieces as the sound device asks for them: the same samples as in one go
        auto whole = tone(0.3, 2.2f); auto pieces = whole;
        PushBoostLimiter a, b; a.process(whole.data(), whole.size() / 2, 2);
        for (std::size_t at = 0; at < pieces.size() / 2;) { const std::size_t n = std::min<std::size_t>(441, pieces.size() / 2 - at); b.process(pieces.data() + at * 2, n, 2); at += n; }
        check(whole == pieces, "block size does not change the result");
    }
    {   // what is not a number, more channels, nothing at all
        std::vector<float> v{std::numeric_limits<float>::quiet_NaN(), 0.5f, std::numeric_limits<float>::infinity(), -4.0f};
        PushBoostLimiter limiter; limiter.process(v.data(), 2, 2);
        check(v[0] == 0.0f && v[2] == 0.0f && std::isfinite(v[1]) && std::abs(v[3]) <= PushBoostLimiter::kCeiling + 1e-6f, "a sample that is not a number becomes silence");
        auto six = tone(0.1, 2.0f, 6); limiter.reset(); limiter.process(six.data(), six.size() / 6, 6);
        check(peakOf(six) <= PushBoostLimiter::kCeiling + 1e-6f, "six channels");
        limiter.process(nullptr, 100, 2); limiter.process(six.data(), 0, 6); limiter.process(six.data(), 10, 0);
        check(true, "nothing to do is nothing done");
        limiter.gain = 0.2f; limiter.reset(); check(limiter.gain == 1.0f, "a reset starts at full volume");
    }
    {   // the volume comes back down to 100 %: no step, and then nothing is touched again
        PushBoostLimiter limiter;
        auto loud = tone(0.2, 3.0f); limiter.process(loud.data(), loud.size() / 2, 2);
        const float held = limiter.gain;
        check(held < 0.4f, "a tone at 300 % is held at a third");
        auto after = tone(1.0, 1.0f); const auto plain = after;
        limiter.finish(after.data(), 480, 2);
        const float firstRatio = std::abs(after[2 * 12]) / std::abs(plain[2 * 12]);   // a sample away from a zero crossing
        check(std::abs(firstRatio - held) < 0.01f, "right after the boost ends the sound is as loud as it was: no jump");
        for (std::size_t at = 480; at < after.size() / 2; at += 480) limiter.finish(after.data() + at * 2, 480, 2);
        check(limiter.gain == 1.0f, "within a second it is back at full volume");
        bool monotone = true; float last = 0.0f;
        for (std::size_t i = 12; i < 40000; i += 109) {                              // every 109th frame: one tone period is 109.09 frames
            const float r = std::abs(plain[2 * i]) > 0.05f ? std::abs(after[2 * i]) / std::abs(plain[2 * i]) : last;
            if (r + 1e-4f < last) monotone = false;
            last = r;
        }
        check(monotone, "and it only ever gets louder on the way");
        auto later = tone(0.1, 1.0f); const auto same = later;
        limiter.finish(later.data(), later.size() / 2, 2);
        check(later == same, "after that the samples pass bit for bit again");
        PushBoostLimiter never; auto untouched = tone(0.1, 1.0f); const auto original = untouched;
        never.finish(untouched.data(), untouched.size() / 2, 2);
        check(untouched == original, "a volume that was never above 100 % touches nothing at all");
    }
    std::printf(failures ? "\nFAILED: %d\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
