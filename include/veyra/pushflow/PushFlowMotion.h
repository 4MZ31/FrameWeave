#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFlow's motion contract: what the tensors a model gives besides its picture mean, how positions
// convert between the motion field and the picture, and a reference of the full-size rebuild. Pure (no
// graphics, builds anywhere, unit-tested in tests/unit/PushFlowMotionTests.cpp); `reference::rebuild`
// is what shaders/PushFlowReconstruct.hlsl is compared against on WARP (PushGpuTests.cpp,
// testPushFlow): change both together.
//
// THE CONTRACT (docs/PUSHFLOW.md has the same in words)
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
//  - the mask is a blend weight, not a confidence and not a probability of error. Measured on the
//    shipped models (scripts/pushflow/prepare_runtime.py motion_report, 19 November 2026): where the
//    network cannot follow a motion its mask still decides for one sample, and where the mask keeps
//    both samples they agree. A mask that keeps both where they differ is what a fade looks like,
//    not what a failure looks like. So "the mask is undecided" cannot be used to find bad pixels:
//    a guard built on it was written, measured and taken out again (docs/PUSHFLOW2.md).
//
// Sizes: the model runs on modelW x modelH (the tier's share of the picture), padded by repeating the
// edge to padW x padH (a multiple of the model's block). Pixel centres are at whole numbers. An output
// pixel p reads grids and mask at  (p + 0.5) * model / src - 0.5  (bilinear, clamped to the model
// area), turns the grid value into a position in the padded model picture, and that into a position in
// the full-size real frame:  (xm + 0.5) * src / model - 0.5  (bilinear, clamped to the frame). So the
// motion is enlarged, never the picture.
#include <algorithm>
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

// --- reference (what the shader does, in the same order) -----------------------------------------
namespace reference {
struct Rgb { float r = 0, g = 0, b = 0; };
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

// One pixel of the rebuilt picture, encoded 0..1. previous(x, y) / current(x, y): the real frames'
// encoded colours; grid0(x, y) / grid1(x, y): the grids at a whole padded-model position; mask(x, y):
// likewise.
template <typename FrameA, typename FrameB, typename Grid0, typename Grid1, typename Mask>
Rgb rebuild(const Geometry& g, uint32_t x, uint32_t y, FrameA&& previous, FrameB&& current, Grid0&& grid0, Grid1&& grid1, Mask&& mask) {
    const float qx = modelCoord(float(x), g.srcW, g.modelW), qy = modelCoord(float(y), g.srcH, g.modelH);
    const Vec2 g0 = bilinear<Vec2>(qx, qy, g.modelW, g.modelH, grid0), g1 = bilinear<Vec2>(qx, qy, g.modelW, g.modelH, grid1);
    const auto sample = [&](Vec2 grid, auto&& frame) {
        return bilinear<Rgb>(gridToSource(grid.x, g.srcW, g.modelW, g.padW, g.alignCorners), gridToSource(grid.y, g.srcH, g.modelH, g.padH, g.alignCorners),
                             g.srcW, g.srcH, frame);
    };
    const auto saturate = [](Rgb c) { return Rgb{std::clamp(c.r, 0.0f, 1.0f), std::clamp(c.g, 0.0f, 1.0f), std::clamp(c.b, 0.0f, 1.0f)}; };
    const Rgb a = saturate(sample(g0, previous)), b = saturate(sample(g1, current));
    const float m = std::clamp(bilinear<float>(qx, qy, g.modelW, g.modelH, mask), 0.0f, 1.0f);
    return g.maskForFirst ? a * m + b * (1.0f - m) : a * (1.0f - m) + b * m;
}
} // namespace reference

} // namespace veyra::pushflow::motion
