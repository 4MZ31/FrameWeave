#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFlow's motion contract and its ghost guard. Pure (no graphics, builds anywhere, unit-tested in
// tests/unit/PushFlowMotionTests.cpp); the functions under `reference` are what
// shaders/PushFlowReconstruct.hlsl is compared against on WARP (PushGpuTests.cpp, testPushFlowGuard):
// change both together.
//
// THE MOTION CONTRACT (docs/PUSHFLOW.md has the same in words)
//
// Besides its picture, a PushFlow model gives three tensors for the in-between frame at time t
// (scripts/pushflow/rife_graph.py exposes them and checks that they reproduce the model's picture):
//
//   grid0, grid1  [padH, padW, 2] floats, x then y. For every pixel of the in-between frame: WHERE TO
//                 READ the previous real frame (grid0) and the current real frame (grid1). GridSample
//                 convention: -1..1 across the padded model picture, `align_corners` as the model
//                 says (<model>.onnx.txt). They are backward sampling positions for this one time t.
//   mask          [padH, padW] 0..1: the share of the previous-frame sample in the blend (of the
//                 current-frame sample when the model's `mask_for_first` is 0).
//
//   picture = sample(previous, grid0) * w + sample(current, grid1) * (1 - w),  w = that share.
//
// What they are NOT:
//  - not optical flow between the two real frames. The network works each time t out on its own,
//    occlusion included; dividing a grid's displacement by t does not give the motion from one real
//    frame to the next, and nothing here pretends it does.
//  - the mask is a blend weight, not a confidence and not a probability of error.
//
// Sizes: the model runs on modelW x modelH (the tier's share of the picture), padded by repeating the
// edge to padW x padH (a multiple of the model's block). Pixel centres are at whole numbers. An output
// pixel p reads grids and mask at  (p + 0.5) * model / src - 0.5  (bilinear, clamped to the model
// area), turns the grid value into a position in the padded model picture, and that into a position in
// the full-size real frame:  (xm + 0.5) * src / model - 0.5  (bilinear, clamped to the frame). So the
// motion is enlarged, never the picture.
//
// THE GHOST GUARD (experimental, off unless pushflow::motion::setGuard was called with 1..3)
//
// Where the network cannot follow the motion, its two samples show different things and its mask
// cannot choose: the blend is then a mixture of two pictures, which is the "ghost" (a see-through
// double image). How far the blend is from the nearer of its two samples is exactly
//     ghost = min(w, 1 - w) * |sampleA - sampleB|        (largest colour channel, encoded 0..1)
// It is 0 where the samples agree and where the mask decides. Where it passes the level's range, the
// guard moves the weight to ONE sample: the one from the real frame nearer in time (the previous
// frame up to t = 0.5, the current one after). The pixel then shows one picture instead of two. At
// worst that region steps at the game's own frame rate, which is what it does without frame
// generation. Nothing is invented and no other pass is added.
//
// What the guard cannot know: whether the result looks better on a real game. The levels below are
// starting values, not tuned ones; they wait for clips and cards (docs/PUSHFLOW2.md).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace veyra::pushflow::motion {

// --- coordinates ---------------------------------------------------------------------------------
struct Geometry {
    uint32_t srcW = 0, srcH = 0;       // the real frames and the picture made
    uint32_t modelW = 0, modelH = 0;   // the part of the padded tensor that holds the picture
    uint32_t padW = 0, padH = 0;       // the tensors' size
    bool alignCorners = true;
    bool maskForFirst = true;
    bool valid() const { return srcW && srcH && modelW && modelH && padW >= modelW && padH >= modelH; }
};
// An output pixel -> where grids and mask are read (continuous model-pixel position).
inline float modelCoord(float outPixel, uint32_t src, uint32_t model) { return (outPixel + 0.5f) * float(model) / float(src) - 0.5f; }
// A grid value (-1..1) -> a position in the padded model picture.
inline float gridToModel(float g, uint32_t pad, bool alignCorners) {
    return alignCorners ? (g + 1.0f) * 0.5f * (float(pad) - 1.0f) : ((g + 1.0f) * float(pad) - 1.0f) * 0.5f;
}
// The grid value that reads padded-model position x (the inverse of gridToModel).
inline float modelToGrid(float x, uint32_t pad, bool alignCorners) {
    if (alignCorners) return pad > 1 ? 2.0f * x / (float(pad) - 1.0f) - 1.0f : 0.0f;
    return (2.0f * x + 1.0f) / float(pad) - 1.0f;
}
// A model position -> a position in the full-size real frame.
inline float modelToSource(float xm, uint32_t src, uint32_t model) { return (xm + 0.5f) * float(src) / float(model) - 0.5f; }
inline float gridToSource(float g, uint32_t src, uint32_t model, uint32_t pad, bool alignCorners) {
    return modelToSource(gridToModel(g, pad, alignCorners), src, model);
}
// The grid value that reads full-size position x (tests and identity grids).
inline float sourceToGrid(float x, uint32_t src, uint32_t model, uint32_t pad, bool alignCorners) {
    return modelToGrid((x + 0.5f) * float(model) / float(src) - 0.5f, pad, alignCorners);
}

// --- the ghost guard -----------------------------------------------------------------------------
inline constexpr int kGuardLevels = 3;   // 1 careful, 2 standard, 3 strong; 0 off
struct GuardRange {
    float lo = 0, hi = 0;                // `ghost` below lo: untouched; above hi: one sample only
    bool on() const { return hi > 0.0f; }
};
// Starting values (see the note at the top). At an even blend (w = 0.5) they mean the two samples
// differ by 0.20..0.40, 0.12..0.28 and 0.06..0.16 of the range: picture noise (a few thousandths)
// and soft gradients stay below every one of them.
inline constexpr GuardRange guardRange(int level) {
    switch (level) {
    case 1: return {0.10f, 0.20f};
    case 2: return {0.06f, 0.14f};
    case 3: return {0.03f, 0.08f};
    default: return {};
    }
}
inline float smoothstep(float lo, float hi, float x) {
    const float t = std::clamp((x - lo) / (hi - lo), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
// How far the blend is from the nearer of its two samples.
inline float ghost(float weightPrevious, float difference) { return std::min(weightPrevious, 1.0f - weightPrevious) * difference; }
// 0 = the network's blend is kept, 1 = one sample only.
inline float guardShare(GuardRange range, float weightPrevious, float difference) {
    return range.on() ? smoothstep(range.lo, range.hi, ghost(weightPrevious, difference)) : 0.0f;
}
// The real frame nearer in time to t: the previous one up to the middle.
inline bool previousNearer(float timestep) { return timestep <= 0.5f; }
// The weight of the previous-frame sample after the guard.
inline float guardedWeight(GuardRange range, float weightPrevious, float difference, bool usePrevious) {
    const float share = guardShare(range, weightPrevious, difference);
    return weightPrevious + ((usePrevious ? 1.0f : 0.0f) - weightPrevious) * share;
}

// Process-wide, set by the interface (preference pushFlowGuard), read by PushFlow at every generated
// frame: live, no graph rebuild, and outside EnhancementSettings so the preset schema is unchanged.
namespace detail {
inline std::atomic<int>& guardStore() {
    static std::atomic<int> level{0};
    return level;
}
}
inline void setGuard(int level) { detail::guardStore().store(std::clamp(level, 0, kGuardLevels), std::memory_order_relaxed); }
inline int guard() { return detail::guardStore().load(std::memory_order_relaxed); }

// --- reference (what the shader does, in the same order) -----------------------------------------
namespace reference {
struct Rgb { float r = 0, g = 0, b = 0; };
struct Pixel {
    Rgb color;            // the picture's pixel, encoded 0..1
    float share = 0;      // the guard's share at this pixel (0 with the guard off)
};
// Bilinear read of a field at a continuous position, clamped to [0, w-1] x [0, h-1].
// at(x, y) returns the value at a whole position.
template <typename T, typename At>
T bilinear(float qx, float qy, uint32_t w, uint32_t h, At&& at) {
    qx = std::clamp(qx, 0.0f, float(w - 1)); qy = std::clamp(qy, 0.0f, float(h - 1));
    const uint32_t ax = uint32_t(qx), ay = uint32_t(qy);
    const uint32_t bx = std::min(ax + 1, w - 1), by = std::min(ay + 1, h - 1);
    const float fx = qx - float(ax), fy = qy - float(ay);
    const T top = at(ax, ay) + (at(bx, ay) - at(ax, ay)) * fx;
    const T bottom = at(ax, by) + (at(bx, by) - at(ax, by)) * fx;
    return top + (bottom - top) * fy;
}
inline Rgb operator+(Rgb a, Rgb b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
inline Rgb operator-(Rgb a, Rgb b) { return {a.r - b.r, a.g - b.g, a.b - b.b}; }
inline Rgb operator*(Rgb a, float s) { return {a.r * s, a.g * s, a.b * s}; }
struct Vec2 { float x = 0, y = 0; };
inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }

// One pixel of the rebuilt picture. previous(x, y) / current(x, y): the real frames' encoded colours;
// grid0(x, y) / grid1(x, y): the grids at a whole padded-model position; mask(x, y): likewise.
template <typename FrameA, typename FrameB, typename Grid0, typename Grid1, typename Mask>
Pixel rebuild(const Geometry& g, uint32_t x, uint32_t y, FrameA&& previous, FrameB&& current, Grid0&& grid0, Grid1&& grid1, Mask&& mask,
              GuardRange range, bool usePrevious) {
    const float qx = modelCoord(float(x), g.srcW, g.modelW), qy = modelCoord(float(y), g.srcH, g.modelH);
    const Vec2 g0 = bilinear<Vec2>(qx, qy, g.modelW, g.modelH, grid0), g1 = bilinear<Vec2>(qx, qy, g.modelW, g.modelH, grid1);
    const auto sample = [&](Vec2 grid, auto&& frame) {
        return bilinear<Rgb>(gridToSource(grid.x, g.srcW, g.modelW, g.padW, g.alignCorners), gridToSource(grid.y, g.srcH, g.modelH, g.padH, g.alignCorners),
                             g.srcW, g.srcH, frame);
    };
    const auto saturate = [](Rgb c) { return Rgb{std::clamp(c.r, 0.0f, 1.0f), std::clamp(c.g, 0.0f, 1.0f), std::clamp(c.b, 0.0f, 1.0f)}; };
    const Rgb a = saturate(sample(g0, previous)), b = saturate(sample(g1, current));
    const float m = std::clamp(bilinear<float>(qx, qy, g.modelW, g.modelH, mask), 0.0f, 1.0f);
    Pixel out;
    out.color = g.maskForFirst ? a * m + b * (1.0f - m) : a * (1.0f - m) + b * m;
    if (range.on()) {
        const float w0 = g.maskForFirst ? m : 1.0f - m;
        const float difference = std::max(std::abs(a.r - b.r), std::max(std::abs(a.g - b.g), std::abs(a.b - b.b)));
        out.share = guardShare(range, w0, difference);
        const float w1 = w0 + ((usePrevious ? 1.0f : 0.0f) - w0) * out.share;
        out.color = a * w1 + b * (1.0f - w1);
    }
    return out;
}
} // namespace reference

} // namespace veyra::pushflow::motion
