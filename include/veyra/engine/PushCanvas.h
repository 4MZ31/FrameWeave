#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS outputs and canvas: what a recording or a stream is made of, as OBS's base canvas and
// output (scaled) resolution.
//  - Output size: each output (recording, instant replay, stream) is encoded at its own size, for
//    example 4K recordings and a 1080p stream at the same time. 0 x 0 is the presented picture's size.
//  - Canvas layout: where the game picture sits on the output. A box (fractions of the output), a crop
//    of the game picture (fractions of each side) and how the cropped picture goes into the box:
//    fit (whole picture, the rest of the box shows the background), fill (covers the box, the excess
//    is cropped evenly: a 16:9 game on a 9:16 output keeps the middle) or stretch. The background is a
//    colour. Scenes and sources are drawn on top in output coordinates.
// The recorder samples the presented picture straight into each output's planes (one pass, box
// filtered when it shrinks: shaders/PushCanvasConvert.hlsl), so nothing is scaled twice.
// Pure and header-only: tests/unit/PushCanvasTests.cpp.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace veyra::engine {

enum class PushCanvasFit : int { Fit = 0, Fill = 1, Stretch = 2 };

struct PushCanvas {
    float x = 0, y = 0, w = 1, h = 1;                       // the game picture's box on the output
    float cropLeft = 0, cropTop = 0, cropRight = 0, cropBottom = 0;  // of the game picture
    PushCanvasFit fit = PushCanvasFit::Fit;
    uint32_t background = 0x000000;                         // 0xRRGGBB, sRGB
    bool operator==(const PushCanvas&) const = default;
    PushCanvas sanitized() const {
        PushCanvas c = *this;
        auto clean = [](float v, float lo, float hi, float fallback) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; };
        c.x = clean(c.x, -1.0f, 1.0f, 0.0f); c.y = clean(c.y, -1.0f, 1.0f, 0.0f);
        c.w = clean(c.w, 0.02f, 3.0f, 1.0f); c.h = clean(c.h, 0.02f, 3.0f, 1.0f);
        c.cropLeft = clean(c.cropLeft, 0.0f, 0.45f, 0.0f); c.cropRight = clean(c.cropRight, 0.0f, 0.45f, 0.0f);
        c.cropTop = clean(c.cropTop, 0.0f, 0.45f, 0.0f); c.cropBottom = clean(c.cropBottom, 0.0f, 0.45f, 0.0f);
        if (int(c.fit) < 0 || int(c.fit) > 2) c.fit = PushCanvasFit::Fit;
        c.background &= 0xFFFFFF;
        return c;
    }
    // The whole picture, unchanged, on the whole output.
    bool fullFrame() const {
        return x == 0 && y == 0 && w == 1 && h == 1 && cropLeft == 0 && cropTop == 0 && cropRight == 0 && cropBottom == 0;
    }
};

// Source rectangle (pixels of the presented picture) and destination rectangle (pixels of the output).
struct PushCanvasMap {
    double srcX = 0, srcY = 0, srcW = 0, srcH = 0;
    double dstX = 0, dstY = 0, dstW = 0, dstH = 0;
};

inline PushCanvasMap mapPushCanvas(const PushCanvas& wanted, uint32_t srcW, uint32_t srcH, uint32_t outW, uint32_t outH) {
    const PushCanvas c = wanted.sanitized();
    PushCanvasMap m;
    if (!srcW || !srcH || !outW || !outH) return m;
    m.srcX = c.cropLeft * srcW; m.srcY = c.cropTop * srcH;
    m.srcW = std::max(2.0, srcW * (1.0 - c.cropLeft - c.cropRight));
    m.srcH = std::max(2.0, srcH * (1.0 - c.cropTop - c.cropBottom));
    const double bx = c.x * outW, by = c.y * outH, bw = c.w * outW, bh = c.h * outH;
    m.dstX = bx; m.dstY = by; m.dstW = bw; m.dstH = bh;
    if (c.fit == PushCanvasFit::Fit) {
        const double scale = std::min(bw / m.srcW, bh / m.srcH);
        m.dstW = m.srcW * scale; m.dstH = m.srcH * scale;
        m.dstX = bx + (bw - m.dstW) / 2; m.dstY = by + (bh - m.dstH) / 2;
    } else if (c.fit == PushCanvasFit::Fill) {
        const double scale = std::max(bw / m.srcW, bh / m.srcH);
        const double visibleW = bw / scale, visibleH = bh / scale;
        m.srcX += (m.srcW - visibleW) / 2; m.srcY += (m.srcH - visibleH) / 2;
        m.srcW = visibleW; m.srcH = visibleH;
    }
    return m;
}

// The size an output is encoded at: the requested size (one side 0 keeps the picture's shape), else the
// picture's own; even, at least 16, and no side above maxSide (shrunk keeping the shape).
inline void pushOutputSize(uint32_t reqW, uint32_t reqH, uint32_t srcW, uint32_t srcH, uint32_t maxSide, uint32_t& w, uint32_t& h) {
    double ow = reqW, oh = reqH;
    if (!reqW && !reqH) { ow = srcW; oh = srcH; }
    else if (!reqW && srcH) ow = double(reqH) * srcW / srcH;
    else if (!reqH && srcW) oh = double(reqW) * srcH / srcW;
    if (maxSide && (ow > maxSide || oh > maxSide)) {
        const double s = double(maxSide) / std::max(ow, oh);
        ow *= s; oh *= s;
    }
    w = std::max(16u, uint32_t(std::lround(ow)) & ~1u);
    h = std::max(16u, uint32_t(std::lround(oh)) & ~1u);
}

// Box-filter taps per axis when the picture shrinks by `scale` (1 when it grows), at most 4.
inline uint32_t pushCanvasTaps(double srcSize, double dstSize) {
    if (!(dstSize > 0) || !(srcSize > 0)) return 1;
    return uint32_t(std::clamp(std::ceil(srcSize / dstSize - 1e-6), 1.0, 4.0));
}

} // namespace veyra::engine
