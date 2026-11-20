// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFlow 2's Response, its first step: a measurement, and nothing on the picture.
// The idea of Response is to turn the picture a little from the stick's movement before the console's own
// frame arrives. Whether that can work from a video signal at all is unproven (docs/PUSHFLOW2.md), so the
// first step only asks the question, on the user's own setup: does the picture's own motion follow the
// right stick, how strongly, and how late?
//
// Two things are observed, both already inside PushFPS: the right stick PushFPS itself sends to the
// console (PS5 Remote Play; a controller plugged into the console is never seen), and how far the whole
// picture moved from one real frame to the next (from PushFlow's own motion, while Predict runs). Nothing
// is recorded to disk, no button is read, nothing is sent anywhere, and it all goes when the part is
// switched off or the source changes.
// Pure: builds with g++ (tests/unit/PushResponseTests.cpp).
#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

namespace veyra::engine::response {

inline constexpr size_t kStickSamples = 1024;          // a few seconds of polling
inline constexpr size_t kMotionSamples = 600;          // 10 to 20 seconds of real frames
inline constexpr int kDelayStepMs = 10, kDelayMaxMs = 400;
inline constexpr float kStickActive = 0.2f;            // below this the stick counts as at rest
inline constexpr size_t kActiveNeeded = 90;            // real frames with the stick held before anything is said
inline constexpr float kFitNeeded = 0.5f;              // share of the motion the stick must explain to be called a fit

// The one clock both observations are stamped with (100 ns).
inline int64_t hostNow100ns() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 100; }

struct Stick { int64_t host100ns = 0; float x = 0, y = 0; };          // -1..1, right and down positive
struct Motion { int64_t host100ns = 0; float x = 0, y = 0; };         // picture widths (x) and heights (y) a second

struct Fit {
    size_t frames = 0, active = 0;     // real frames seen, and of them with the stick held
    bool enough = false;               // enough stick movement to say anything
    bool follows = false;              // the picture's motion follows the stick
    float delayMs = 0;                 // how long after the stick the picture moves
    float gainX = 0, gainY = 0;        // picture widths / heights a second at full stick (signed: a negative x turns with the stick)
    float explained = 0;               // share of the picture's motion the stick explains, 0..1
};

class Observer {
public:
    // The stick as it was just sent (any thread). Kept only while `listening`.
    void stick(int64_t host100ns, float x, float y) {
        std::lock_guard lock(m_);
        if (!listening_) return;
        sticks_[stickNext_ % kStickSamples] = Stick{host100ns, std::clamp(x, -1.0f, 1.0f), std::clamp(y, -1.0f, 1.0f)};
        ++stickNext_;
    }
    // How far the whole picture moved over one real frame interval that ended at `host100ns`.
    void motion(int64_t host100ns, float widthsPerSecond, float heightsPerSecond) {
        std::lock_guard lock(m_);
        if (!listening_ || !std::isfinite(widthsPerSecond) || !std::isfinite(heightsPerSecond)) return;
        motions_[motionNext_ % kMotionSamples] = Motion{host100ns, widthsPerSecond, heightsPerSecond};
        ++motionNext_;
    }
    // Off: everything observed is thrown away at once, and nothing more is kept.
    void listen(bool on) { std::lock_guard lock(m_); if (on != listening_) { listening_ = on; clearLocked(); } }
    bool listening() const { std::lock_guard lock(m_); return listening_; }
    // A new source or a break in it: what was seen belongs to another picture.
    void clear() { std::lock_guard lock(m_); clearLocked(); }
    size_t sticksSeen() const { std::lock_guard lock(m_); return std::min(stickNext_, kStickSamples); }

    Fit fit() const {
        std::vector<Stick> sticks; std::vector<Motion> motions;
        {
            std::lock_guard lock(m_);
            const size_t ns = std::min(stickNext_, kStickSamples), nm = std::min(motionNext_, kMotionSamples);
            sticks.reserve(ns); motions.reserve(nm);
            for (size_t i = 0; i < ns; ++i) sticks.push_back(sticks_[(stickNext_ - ns + i) % kStickSamples]);
            for (size_t i = 0; i < nm; ++i) motions.push_back(motions_[(motionNext_ - nm + i) % kMotionSamples]);
        }
        Fit best; best.frames = motions.size();
        if (sticks.size() < 8 || motions.size() < 8) return best;
        // The stick at a time: the last sample at or before it (it is held between polls).
        const auto at = [&](int64_t t, Stick& out) {
            const auto it = std::upper_bound(sticks.begin(), sticks.end(), t, [](int64_t v, const Stick& s) { return v < s.host100ns; });
            if (it == sticks.begin()) return false;
            out = *(it - 1);
            return t - out.host100ns < 5000000;   // older than half a second: the pad was not being read
        };
        double bestScore = -1;
        for (int delay = 0; delay <= kDelayMaxMs; delay += kDelayStepMs) {
            double sxx = 0, sxm = 0, syy = 0, sym = 0, mm = 0; size_t active = 0;
            for (const Motion& m : motions) {
                Stick s;
                if (!at(m.host100ns - int64_t(delay) * 10000, s)) continue;
                if (std::max(std::abs(s.x), std::abs(s.y)) < kStickActive) continue;
                ++active;
                sxx += double(s.x) * s.x; sxm += double(s.x) * m.x; syy += double(s.y) * s.y; sym += double(s.y) * m.y;
                mm += double(m.x) * m.x + double(m.y) * m.y;
            }
            if (active < kActiveNeeded || mm <= 1e-9) { if (active > best.active) best.active = active; continue; }
            // Least squares through the origin, each axis on its own; what is left over is not explained.
            const double gx = sxx > 1e-9 ? sxm / sxx : 0.0, gy = syy > 1e-9 ? sym / syy : 0.0;
            const double explained = std::clamp((gx * sxm + gy * sym) / mm, 0.0, 1.0);
            if (explained > bestScore) {
                bestScore = explained;
                best.active = active; best.enough = true; best.delayMs = float(delay); best.gainX = float(gx); best.gainY = float(gy); best.explained = float(explained);
            }
        }
        best.follows = best.enough && best.explained >= kFitNeeded;
        return best;
    }

private:
    void clearLocked() { stickNext_ = motionNext_ = 0; }
    mutable std::mutex m_;
    bool listening_ = false;
    std::array<Stick, kStickSamples> sticks_{};
    std::array<Motion, kMotionSamples> motions_{};
    size_t stickNext_ = 0, motionNext_ = 0;
};

// The one observer of the program.
inline Observer& observer() { static Observer o; return o; }

} // namespace veyra::engine::response
