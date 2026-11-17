// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS streaming mode sources: the platform-free per-frame touches (engine/PushLayerSources.h).
#include "veyra/engine/PushLayerSources.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

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

} // namespace veyra::engine
