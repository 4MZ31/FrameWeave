// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFlow's motion contract and ghost guard (pushflow/PushFlowMotion.h, pure):
//  - grid values, model positions and full-size positions convert both ways, for both corner
//    conventions, odd sizes, padding and a network smaller than the picture;
//  - the reference rebuild reads the real frames where the grids point (still, shifted by whole and
//    by part pixels, off the edge) and blends them with the mask, either way round;
//  - the guard is exactly nothing when off, leaves agreeing samples and decided masks alone, turns a
//    mixture of two different pictures into one of them (the frame nearer in time), never leaves the
//    range between the two samples, and moves smoothly.
#include "veyra/pushflow/PushFlowMotion.h"
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
using namespace veyra::pushflow::motion;
using reference::Rgb;
using reference::Vec2;
static int failures = 0;
static void check(bool ok, const std::string& label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str()); if (!ok) ++failures; }
static bool about(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }

// A small scene: frames, grids and mask as the backend holds them.
struct Scene {
    Geometry g;
    std::vector<Rgb> previous, current;
    std::vector<Vec2> grid0, grid1;   // padW x padH
    std::vector<float> mask;          // padW x padH
    explicit Scene(Geometry geometry) : g(geometry) {
        previous.resize(size_t(g.srcW) * g.srcH); current.resize(previous.size());
        grid0.resize(size_t(g.padW) * g.padH); grid1.resize(grid0.size()); mask.assign(grid0.size(), 0.5f);
        shift(grid0, 0, 0); shift(grid1, 0, 0);
    }
    // Grids that read the frame `dx`, `dy` full-size pixels away from each pixel.
    void shift(std::vector<Vec2>& grid, float dx, float dy) const {
        for (uint32_t y = 0; y < g.padH; ++y)
            for (uint32_t x = 0; x < g.padW; ++x) {
                // The full-size position this model pixel stands for, moved.
                const float sx = modelToSource(float(x), g.srcW, g.modelW) + dx, sy = modelToSource(float(y), g.srcH, g.modelH) + dy;
                grid[size_t(y) * g.padW + x] = {sourceToGrid(sx, g.srcW, g.modelW, g.padW, g.alignCorners), sourceToGrid(sy, g.srcH, g.modelH, g.padH, g.alignCorners)};
            }
    }
    reference::Pixel at(uint32_t x, uint32_t y, GuardRange range = {}, bool usePrevious = true) const {
        return reference::rebuild(g, x, y,
            [&](uint32_t px, uint32_t py) { return previous[size_t(py) * g.srcW + px]; }, [&](uint32_t px, uint32_t py) { return current[size_t(py) * g.srcW + px]; },
            [&](uint32_t px, uint32_t py) { return grid0[size_t(py) * g.padW + px]; }, [&](uint32_t px, uint32_t py) { return grid1[size_t(py) * g.padW + px]; },
            [&](uint32_t px, uint32_t py) { return mask[size_t(py) * g.padW + px]; }, range, usePrevious);
    }
};
// A picture whose value is a smooth function of position, so part-pixel reads can be checked exactly.
static Rgb ramp(float x, float y, uint32_t w, uint32_t h) { return {x / float(w - 1), y / float(h - 1), 0.25f}; }

int main() {
    // --- coordinates -----------------------------------------------------------------------------
    {
        bool roundTrip = true, centres = true;
        for (bool align : {true, false})
            for (uint32_t pad : {1u, 2u, 31u, 64u, 257u, 1920u})
                for (float x : {0.0f, 0.5f, 1.0f, 7.25f, float(pad) - 1.0f}) {
                    if (pad == 1 && align) continue;   // one sample: every grid value reads it
                    roundTrip = roundTrip && about(gridToModel(modelToGrid(x, pad, align), pad, align), x, 1e-3f);
                }
        check(roundTrip, "grid value <-> padded model position converts both ways (both corner conventions, odd sizes)");
        // GridSample: with align_corners -1 and +1 are the centres of the first and last sample; without,
        // they are the outer edges of the first and last sample.
        centres = about(gridToModel(-1, 64, true), 0) && about(gridToModel(1, 64, true), 63) && about(gridToModel(-1, 64, false), -0.5f) && about(gridToModel(1, 64, false), 63.5f);
        check(centres, "-1 and +1 mean the outer sample centres with align_corners and the outer edges without");
        check(about(modelCoord(0, 3840, 960), -0.375f) && about(modelCoord(3839, 3840, 960), 959.375f), "a 4K pixel reads a 960-wide motion field at its own centre");
        check(about(modelToSource(modelCoord(1234, 3840, 960), 3840, 960), 1234, 1e-2f), "picture -> motion field -> picture comes back to the same pixel");
        bool identity = true;
        for (bool align : {true, false})
            for (uint32_t src : {256u, 1920u, 3840u})
                for (uint32_t model : {128u, 960u})
                    for (uint32_t pad : {model, model + 32})
                        for (float x : {0.0f, 3.0f, 100.5f, float(src) - 1.0f})
                            identity = identity && about(gridToSource(sourceToGrid(x, src, model, pad, align), src, model, pad, align), x, 2e-2f);
        check(identity, "the grid value made for a full-size position reads that position (padding and a smaller network included)");
        check(!Geometry{}.valid() && Geometry{256, 160, 128, 80, 128, 96}.valid() && !Geometry{256, 160, 128, 80, 64, 96}.valid(), "a geometry needs sizes, and padding never smaller than the model");
    }
    // --- the rebuild -----------------------------------------------------------------------------
    for (bool align : {true, false})
        for (bool half : {false, true}) {
            // 96 x 64 picture; the network at full or half size, padded to a multiple of 32.
            Geometry g{96, 64, half ? 48u : 96u, half ? 32u : 64u, half ? 64u : 96u, half ? 32u : 64u, align, true};
            const std::string name = std::string(align ? "align_corners" : "edges") + (half ? ", network at half size" : ", network at full size");
            Scene s(g);
            for (uint32_t y = 0; y < g.srcH; ++y)
                for (uint32_t x = 0; x < g.srcW; ++x) { s.previous[size_t(y) * g.srcW + x] = ramp(float(x), float(y), g.srcW, g.srcH); s.current[size_t(y) * g.srcW + x] = s.previous[size_t(y) * g.srcW + x]; }
            // Exact everywhere inside. A smaller motion field ends half a model pixel before the
            // picture does, and its last value is repeated there: the outermost picture pixel of a
            // still pair reads up to half a pixel inwards. That is how the rebuild has always worked
            // (rebuild_scaled in scripts/pushflow/prepare_runtime.py); recorded here, not changed.
            bool still = true, border = true;
            for (uint32_t y = 0; y < g.srcH; ++y)
                for (uint32_t x = 0; x < g.srcW; ++x) {
                    const auto p = s.at(x, y); const Rgb want = ramp(float(x), float(y), g.srcW, g.srcH);
                    const bool edge = x == 0 || y == 0 || x == g.srcW - 1 || y == g.srcH - 1;
                    if (!edge) still = still && about(p.color.r, want.r, 2e-3f) && about(p.color.g, want.g, 2e-3f) && p.share == 0.0f;
                    else border = border && about(p.color.r, want.r, 0.6f / float(g.srcW - 1)) && about(p.color.g, want.g, 0.6f / float(g.srcH - 1));
                }
            check(still, "a still pair comes back as it is (" + name + ")");
            check(border, "at the picture's outermost pixel a still pair is at most half a pixel off (" + name + ")");
            // The previous frame read 3.5 pixels to the right, the current one 2 down: on a ramp the
            // value moves by exactly that much, away from the edges.
            s.shift(s.grid0, 3.5f, 0); s.shift(s.grid1, 0, 2.0f);
            std::fill(s.mask.begin(), s.mask.end(), 1.0f);
            const auto fromPrevious = s.at(40, 30);
            std::fill(s.mask.begin(), s.mask.end(), 0.0f);
            const auto fromCurrent = s.at(40, 30);
            check(about(fromPrevious.color.r, ramp(43.5f, 30, g.srcW, g.srcH).r, 2e-3f) && about(fromPrevious.color.g, ramp(40, 30, g.srcW, g.srcH).g, 2e-3f),
                  "mask 1 shows the previous frame where its grid points, part pixels included (" + name + ")");
            check(about(fromCurrent.color.g, ramp(40, 32, g.srcW, g.srcH).g, 2e-3f) && about(fromCurrent.color.r, ramp(40, 30, g.srcW, g.srcH).r, 2e-3f),
                  "mask 0 shows the current frame where its grid points (" + name + ")");
            // The other way round for models whose mask is the current frame's share.
            Scene r(g); r.g.maskForFirst = false; r.previous = s.previous; r.current = s.current; r.grid0 = s.grid0; r.grid1 = s.grid1;
            std::fill(r.mask.begin(), r.mask.end(), 0.0f);
            check(about(r.at(40, 30).color.r, fromPrevious.color.r, 1e-5f), "a mask that is the current frame's share is read the other way round (" + name + ")");
            // Off the edge: the frame's edge repeats, nothing wraps and nothing is black.
            s.shift(s.grid0, 500.0f, 0); std::fill(s.mask.begin(), s.mask.end(), 1.0f);
            check(about(s.at(40, 30).color.r, 1.0f, 2e-3f), "a read off the right edge gives the edge (" + name + ")");
            s.shift(s.grid0, -500.0f, -500.0f);
            check(about(s.at(40, 30).color.r, 0.0f, 2e-3f) && about(s.at(40, 30).color.g, 0.0f, 2e-3f), "a read off the top left corner gives the corner (" + name + ")");
        }
    // --- the guard -------------------------------------------------------------------------------
    {
        check(!guardRange(0).on() && !guardRange(-1).on() && !guardRange(4).on(), "level 0 and unknown levels are off");
        bool ordered = true;
        for (int level = 1; level <= kGuardLevels; ++level) {
            const auto r = guardRange(level);
            ordered = ordered && r.on() && r.lo > 0 && r.hi > r.lo && r.hi <= 0.5f;
            if (level > 1) ordered = ordered && r.lo < guardRange(level - 1).lo && r.hi < guardRange(level - 1).hi;
        }
        check(ordered, "every level has a range inside what a blend can reach, and a higher level starts sooner");
        setGuard(2); const bool two = guard() == 2; setGuard(99); const bool top = guard() == kGuardLevels; setGuard(-5); const bool off = guard() == 0;
        check(two && top && off, "the process-wide level is kept and clamped");
        check(previousNearer(0.25f) && previousNearer(0.5f) && !previousNearer(0.75f) && !previousNearer(2.0f / 3.0f), "the previous frame is the nearer one up to the middle");

        bool offExact = true, bounded = true, monotone = true, agree = true, decided = true;
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        for (int i = 0; i < 20000; ++i) {
            const float w = unit(rng), d = unit(rng);
            offExact = offExact && guardShare({}, w, d) == 0.0f && guardedWeight({}, w, d, true) == w && guardedWeight({}, w, d, false) == w;
            for (int level = 1; level <= kGuardLevels; ++level) {
                const auto r = guardRange(level);
                const float share = guardShare(r, w, d);
                const float wp = guardedWeight(r, w, d, true), wc = guardedWeight(r, w, d, false);
                bounded = bounded && share >= 0.0f && share <= 1.0f && wp >= w - 1e-6f && wp <= 1.0f + 1e-6f && wc <= w + 1e-6f && wc >= -1e-6f;
                monotone = monotone && guardShare(r, w, std::min(1.0f, d + 0.1f)) >= share - 1e-6f;
                if (level > 1) monotone = monotone && share >= guardShare(guardRange(level - 1), w, d) - 1e-6f;
            }
            // Samples that agree (a difference below every level's start even at an even blend) are never touched.
            agree = agree && guardShare(guardRange(3), w, 0.05f * d) == 0.0f;
            // A mask that has decided (within 2 % of one sample) is never touched, whatever the samples show.
            decided = decided && guardShare(guardRange(3), 0.02f * w, d) == 0.0f && guardShare(guardRange(3), 1.0f - 0.02f * w, d) == 0.0f;
        }
        check(offExact, "off: the weight comes back bit for bit");
        check(bounded, "on: the share is 0..1 and the weight only moves towards the chosen sample");
        check(monotone, "more disagreement never means less guarding, and a higher level never guards less");
        check(agree, "samples that agree are left to the network's blend");
        check(decided, "where the mask has chosen a sample (occlusion), the guard does nothing");
        bool smooth = true;
        for (int level = 1; level <= kGuardLevels; ++level)
            for (float d = 0; d < 1.0f; d += 0.001f) smooth = smooth && std::fabs(guardShare(guardRange(level), 0.5f, d + 0.001f) - guardShare(guardRange(level), 0.5f, d)) < 0.05f;
        check(smooth, "the share rises smoothly: no pixel flips between two pictures on a small change");
    }
    // --- the guard in a picture: a ghost becomes one picture ---------------------------------------
    {
        // A bright square moved 40 pixels between two frames; the network found no motion (identity
        // grids) and could not choose (mask 0.5): the blend shows the square twice at half strength.
        Geometry g{128, 64, 128, 64, 128, 64, true, true};
        Scene s(g);
        const auto draw = [&](std::vector<Rgb>& frame, uint32_t left) {
            for (uint32_t y = 0; y < g.srcH; ++y)
                for (uint32_t x = 0; x < g.srcW; ++x) {
                    const bool in = x >= left && x < left + 20 && y >= 20 && y < 44;
                    frame[size_t(y) * g.srcW + x] = in ? Rgb{0.9f, 0.85f, 0.2f} : Rgb{0.1f, 0.12f, 0.15f + 0.002f * float(x % 3)};
                }
        };
        draw(s.previous, 30); draw(s.current, 70);
        const Rgb square{0.9f, 0.85f, 0.2f};
        const auto ghostPixel = s.at(35, 30), elsewhere = s.at(110, 10);
        check(about(ghostPixel.color.r, 0.5f, 1e-3f) && ghostPixel.share == 0.0f, "without the guard the square's old place is a half-strength mixture");
        for (int level = 1; level <= kGuardLevels; ++level) {
            const auto range = guardRange(level);
            const auto early = s.at(35, 30, range, true), late = s.at(35, 30, range, false);
            const auto newPlaceEarly = s.at(75, 30, range, true), newPlaceLate = s.at(75, 30, range, false);
            const std::string n = "level " + std::to_string(level);
            check(about(early.color.r, square.r, 1e-3f) && about(newPlaceEarly.color.r, 0.1f, 1e-3f) && early.share == 1.0f,
                  n + ": up to the middle the square is shown once, where the previous frame has it");
            check(about(late.color.r, 0.1f, 1e-3f) && about(newPlaceLate.color.r, square.r, 1e-3f), n + ": after the middle it is shown once, where the current frame has it");
            const auto calm = s.at(110, 10, range, true);
            check(calm.share == 0.0f && calm.color.r == elsewhere.color.r && calm.color.b == elsewhere.color.b, n + ": the background, where both frames agree, is the network's own blend, bit for bit");
        }
        // Whatever the mask and the level, a guarded pixel lies between its two samples: nothing new is made.
        bool between = true;
        for (float m = 0; m <= 1.0f; m += 0.05f) {
            std::fill(s.mask.begin(), s.mask.end(), m);
            for (int level = 0; level <= kGuardLevels; ++level)
                for (bool usePrevious : {true, false}) {
                    const auto p = s.at(35, 30, guardRange(level), usePrevious);
                    between = between && p.color.r >= 0.1f - 1e-4f && p.color.r <= 0.9f + 1e-4f && p.color.g >= 0.12f - 1e-4f && p.color.g <= 0.85f + 1e-4f;
                }
        }
        check(between, "a guarded pixel always lies between its two samples: the guard chooses, it never invents");
        // A fade: the whole picture gets brighter by 0.3 with nothing moving. Every level treats it as
        // two pictures that differ (it cannot tell a fade from a ghost at one pixel): it holds the
        // nearer frame instead of blending. Recorded here so a change to that is a decision, not an accident.
        Scene fade(g);
        for (auto& c : fade.previous) c = {0.3f, 0.3f, 0.3f};
        for (auto& c : fade.current) c = {0.6f, 0.6f, 0.6f};
        check(about(fade.at(64, 32).color.r, 0.45f, 1e-4f) && about(fade.at(64, 32, guardRange(3), true).color.r, 0.3f, 1e-3f) && about(fade.at(64, 32, guardRange(1), true).color.r, 0.45f, 0.08f),
              "known limit: a fast fade is held at the strong level and mostly blended at the careful one");
    }
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
