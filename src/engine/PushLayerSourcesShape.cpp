// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS streaming mode sources: the platform-free per-frame touches (engine/PushLayerSources.h).
#include "veyra/engine/PushLayerSources.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace veyra::engine {

void shapePushSourceFrame(PushOverlayImage& image, bool mirror, PushSourceShape shape) {
    const uint32_t W = image.width, H = image.height;
    if (W < 2 || H < 2 || image.rgba.size() < size_t(W) * H * 4) return;
    if (mirror)
        for (uint32_t y = 0; y < H; ++y) {
            uint8_t* row = &image.rgba[size_t(y) * W * 4];
            for (uint32_t x = 0; x < W / 2; ++x)
                for (int c = 0; c < 4; ++c) std::swap(row[size_t(x) * 4 + c], row[size_t(W - 1 - x) * 4 + c]);
        }
    if (shape == PushSourceShape::Rectangle) return;
    if (shape == PushSourceShape::Circle && W != H) {
        // The centred square.
        const uint32_t side = std::min(W, H), left = (W - side) / 2, top = (H - side) / 2;
        std::vector<uint8_t> square(size_t(side) * side * 4);
        for (uint32_t y = 0; y < side; ++y)
            std::memcpy(&square[size_t(y) * side * 4], &image.rgba[(size_t(top + y) * W + left) * 4], size_t(side) * 4);
        image.rgba = std::move(square);
        image.width = image.height = side;
    }
    const uint32_t w = image.width, h = image.height;
    // Distance outside the rounded rectangle (a circle is the fully rounded square), one pixel of
    // anti-aliasing at the edge.
    const double radius = shape == PushSourceShape::Circle ? w / 2.0 : std::min(w, h) * 0.08;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const double px = x + 0.5, py = y + 0.5;
            const double cx = std::clamp(px, radius, w - radius), cy = std::clamp(py, radius, h - radius);
            const double d = std::hypot(px - cx, py - cy) - radius;
            if (d <= -0.5) continue;
            const double keep = d >= 0.5 ? 0.0 : 0.5 - d;
            uint8_t& a = image.rgba[(size_t(y) * w + x) * 4 + 3];
            a = uint8_t(std::lround(a * keep));
        }
}

bool padPushOverlayToShape(const PushOverlayImage& image, uint32_t width, uint32_t height, PushOverlayImage& out) {
    const uint32_t W = image.width, H = image.height;
    if (W < 2 || H < 2 || width < 2 || height < 2 || image.rgba.size() < size_t(W) * H * 4) return false;
    const double want = double(width) / height, have = double(W) / H;
    if (std::abs(have - want) / want < 0.01) return false;
    const uint32_t padW = have < want ? uint32_t(std::lround(H * want)) : W;
    const uint32_t padH = have < want ? H : uint32_t(std::lround(W / want));
    if (padW < W || padH < H || size_t(padW) * padH > size_t(16384) * 16384) return false;
    const auto alpha = [&](uint32_t x, uint32_t y) { return image.rgba[(size_t(y) * W + x) * 4 + 3]; };
    const bool opaque = alpha(0, 0) == 255 && alpha(W - 1, 0) == 255 && alpha(0, H - 1) == 255 && alpha(W - 1, H - 1) == 255;
    out = {};
    out.width = padW; out.height = padH;
    out.rgba.assign(size_t(padW) * padH * 4, 0);
    if (opaque) for (size_t i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = 255;
    const uint32_t ox = (padW - W) / 2, oy = (padH - H) / 2;
    for (uint32_t y = 0; y < H; ++y) std::memcpy(&out.rgba[(size_t(oy + y) * padW + ox) * 4], &image.rgba[size_t(y) * W * 4], size_t(W) * 4);
    return true;
}

void cropPushSourceFrame(PushOverlayImage& image, float left, float top, float right, float bottom) {
    const uint32_t W = image.width, H = image.height;
    if (W < 2 || H < 2 || image.rgba.size() < size_t(W) * H * 4) return;
    auto clean = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 0.45f) : 0.0f; };
    left = clean(left); top = clean(top); right = clean(right); bottom = clean(bottom);
    if (left == 0 && top == 0 && right == 0 && bottom == 0) return;
    const uint32_t x0 = uint32_t(std::lround(left * W)), y0 = uint32_t(std::lround(top * H));
    const uint32_t x1 = std::max(x0 + 2, W - uint32_t(std::lround(right * W))), y1 = std::max(y0 + 2, H - uint32_t(std::lround(bottom * H)));
    const uint32_t w = std::min(x1, W) - x0, h = std::min(y1, H) - y0;
    std::vector<uint8_t> out(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y) std::memcpy(&out[size_t(y) * w * 4], &image.rgba[(size_t(y0 + y) * W + x0) * 4], size_t(w) * 4);
    image.rgba = std::move(out);
    image.width = w; image.height = h;
}

} // namespace veyra::engine
