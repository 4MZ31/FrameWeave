// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS streaming mode sources: the platform-free per-frame touches (engine/PushLayerSources.h).
#include "veyra/engine/PushLayerSources.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

namespace veyra::engine {

namespace {

// Averages `width` x `height` four-byte pixels (rows `pitch` bytes apart) down to outW x outH. Every
// source pixel falls in exactly one box. bgrx: B, G, R, unused in; R, G, B, 255 out.
void boxScale(const uint8_t* source, ptrdiff_t pitch, uint32_t width, uint32_t height, uint32_t outW, uint32_t outH, bool bgrx,
              std::vector<uint8_t>& out) {
    out.resize(size_t(outW) * outH * 4);
    std::vector<uint32_t> edges(size_t(outW) + 1);
    for (uint32_t x = 0; x <= outW; ++x) edges[x] = uint32_t(uint64_t(x) * width / outW);
    std::vector<uint32_t> sums(size_t(outW) * 4);
    for (uint32_t oy = 0; oy < outH; ++oy) {
        const uint32_t y0 = uint32_t(uint64_t(oy) * height / outH);
        const uint32_t y1 = std::min(height, std::max(y0 + 1, uint32_t(uint64_t(oy + 1) * height / outH)));
        std::fill(sums.begin(), sums.end(), 0u);
        for (uint32_t y = y0; y < y1; ++y) {
            const uint8_t* row = source + ptrdiff_t(y) * pitch;
            for (uint32_t ox = 0; ox < outW; ++ox) {
                uint32_t a = 0, b = 0, c = 0, d = 0;
                const uint8_t* p = row + size_t(edges[ox]) * 4;
                const uint8_t* const last = row + size_t(edges[ox + 1]) * 4;
                for (; p < last; p += 4) { a += p[0]; b += p[1]; c += p[2]; d += p[3]; }
                uint32_t* sum = &sums[size_t(ox) * 4];
                sum[0] += a; sum[1] += b; sum[2] += c; sum[3] += d;
            }
        }
        uint8_t* target = &out[size_t(oy) * outW * 4];
        for (uint32_t ox = 0; ox < outW; ++ox) {
            const uint32_t count = std::max(1u, (edges[ox + 1] - edges[ox]) * (y1 - y0)), half = count / 2;
            const uint32_t* sum = &sums[size_t(ox) * 4];
            if (bgrx) {
                target[ox * 4 + 0] = uint8_t((sum[2] + half) / count);
                target[ox * 4 + 1] = uint8_t((sum[1] + half) / count);
                target[ox * 4 + 2] = uint8_t((sum[0] + half) / count);
                target[ox * 4 + 3] = 255;
            } else {
                for (int c = 0; c < 4; ++c) target[ox * 4 + c] = uint8_t((sum[c] + half) / count);
            }
        }
    }
}

uint32_t fittedHeight(uint32_t width, uint32_t height, uint32_t outW) {
    return std::clamp(uint32_t(std::lround(double(height) * outW / width)), 2u, height);
}

} // namespace

void pushBgrxToRgba(const uint8_t* source, ptrdiff_t pitch, uint32_t width, uint32_t height, uint32_t maxWidth, PushOverlayImage& out) {
    if (!source || width < 2 || height < 2) { out.width = out.height = 0; out.rgba.clear(); return; }
    if (maxWidth >= 2 && maxWidth < width) {
        const uint32_t outH = fittedHeight(width, height, maxWidth);
        boxScale(source, pitch, width, height, maxWidth, outH, true, out.rgba);
        out.width = maxWidth; out.height = outH;
        return;
    }
    out.width = width; out.height = height;
    out.rgba.resize(size_t(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = source + ptrdiff_t(y) * pitch;
        uint8_t* target = &out.rgba[size_t(y) * width * 4];
        if constexpr (std::endian::native == std::endian::little) {
            // A pixel as one word: B | G << 8 | R << 16 | unused << 24. Red and blue change places.
            for (uint32_t x = 0; x < width; ++x) {
                uint32_t v;
                std::memcpy(&v, row + size_t(x) * 4, 4);
                v = 0xFF000000u | (v & 0x0000FF00u) | ((v >> 16) & 0xFFu) | ((v & 0xFFu) << 16);
                std::memcpy(target + size_t(x) * 4, &v, 4);
            }
        } else {
            for (uint32_t x = 0; x < width; ++x) {
                target[x * 4 + 0] = row[x * 4 + 2];
                target[x * 4 + 1] = row[x * 4 + 1];
                target[x * 4 + 2] = row[x * 4 + 0];
                target[x * 4 + 3] = 255;
            }
        }
    }
}

void fitPushSourceFrame(PushOverlayImage& image, uint32_t maxWidth) {
    const uint32_t W = image.width, H = image.height;
    if (maxWidth < 2 || W <= maxWidth || W < 2 || H < 2 || image.rgba.size() < size_t(W) * H * 4) return;
    const uint32_t outH = fittedHeight(W, H, maxWidth);
    std::vector<uint8_t> out;
    boxScale(image.rgba.data(), ptrdiff_t(W) * 4, W, H, maxWidth, outH, false, out);
    image.rgba = std::move(out);
    image.width = maxWidth; image.height = outH;
}

uint32_t pushSourceShownWidth(float share, uint32_t referenceWidth) {
    if (!std::isfinite(share) || !(share > 0)) return 0;
    const double reference = double(std::clamp<uint32_t>(referenceWidth, 1920, 8192));
    const double pixels = std::min(double(share), 1.0) * reference;
    return std::max<uint32_t>(320, uint32_t(std::ceil(pixels / 160.0)) * 160);
}

uint32_t pushSourceFitWidth(uint32_t width, uint32_t height, uint32_t shownWidth, PushSourceShape shape) {
    if (!shownWidth || width < 2 || height < 2) return width;
    // A circle shows the centred square: on a wide frame that is `height` pixels of the width.
    uint64_t want = shownWidth;
    if (shape == PushSourceShape::Circle && width > height) want = (uint64_t(shownWidth) * width + height - 1) / height;
    want = std::max<uint64_t>(want, 2);
    return want * 5 <= uint64_t(width) * 4 ? uint32_t(want) : width;
}

int choosePushCameraFormat(const std::vector<PushCameraFormat>& formats, uint32_t wantHeight) {
    const bool automatic = wantHeight == 0;
    const double targetW = automatic ? 1280.0 : std::round(double(wantHeight) * 16.0 / 9.0), targetH = automatic ? 720.0 : double(wantHeight);
    int best = -1;
    double bestScore = 1e18;
    for (size_t i = 0; i < formats.size(); ++i) {
        const auto& f = formats[i];
        if (!f.width || !f.height) continue;
        double score = std::abs(double(f.width) - targetW) + std::abs(double(f.height) - targetH);
        if (automatic) {
            // Unchanged since before 3.2.3: what every camera has been opened with.
            if (f.width > 1920) score += 1e6;
            if (f.fps < 29) score += 1e5 + (30 - f.fps) * 100;
        } else {
            // A chosen size: 24 and 25 FPS count (many cameras send 4K only at those), and of one
            // size's frame rates 30 or more wins over them.
            if (f.fps < 23.5) score += 1e5 + (30 - f.fps) * 100;
            else if (f.fps < 29) score += 30 - f.fps;
        }
        if (score < bestScore) { bestScore = score; best = int(i); }
    }
    return best;
}

std::vector<PushCameraFormat> pushCameraSizes(const std::vector<PushCameraFormat>& formats) {
    std::vector<PushCameraFormat> sizes;
    for (const auto& f : formats) {
        if (!f.width || !f.height) continue;
        auto it = std::find_if(sizes.begin(), sizes.end(), [&](const PushCameraFormat& s) { return s.width == f.width && s.height == f.height; });
        if (it == sizes.end()) sizes.push_back(f);
        else if (f.fps > it->fps) *it = f;
    }
    std::sort(sizes.begin(), sizes.end(), [](const PushCameraFormat& a, const PushCameraFormat& b) {
        const uint64_t areaA = uint64_t(a.width) * a.height, areaB = uint64_t(b.width) * b.height;
        return areaA != areaB ? areaA > areaB : a.width > b.width;
    });
    return sizes;
}

void shapePushSourceFrame(PushOverlayImage& image, bool mirror, PushSourceShape shape) {
    const uint32_t W = image.width, H = image.height;
    if (W < 2 || H < 2 || image.rgba.size() < size_t(W) * H * 4) return;
    if (mirror)
        for (uint32_t y = 0; y < H; ++y) {
            uint8_t* row = &image.rgba[size_t(y) * W * 4];
            for (uint32_t x = 0; x < W / 2; ++x) {
                uint32_t left, right;
                std::memcpy(&left, row + size_t(x) * 4, 4);
                std::memcpy(&right, row + size_t(W - 1 - x) * 4, 4);
                std::memcpy(row + size_t(x) * 4, &right, 4);
                std::memcpy(row + size_t(W - 1 - x) * 4, &left, 4);
            }
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
    // anti-aliasing at the edge: a pixel keeps its alpha where that distance d <= -0.5, loses it where
    // d >= 0.5 and keeps 0.5 - d of it between.
    //
    // Only the corners can be outside, and of a corner's row only a few pixels sit on the edge, so each
    // row is split in three: left of the edge (alpha 0), the edge itself (the distance, exactly as
    // above) and the inside (untouched). The split leaves a pixel's margin on both sides of the edge,
    // so every pixel the distance could change is still computed with it. Until 3.2.3 all of them
    // were, every frame (tests/unit/PushStudioSourcesTests.cpp compares the two, pixel for pixel).
    const double radius = shape == PushSourceShape::Circle ? w / 2.0 : std::min(w, h) * 0.08;
    // A picture a few pixels across has a radius under one pixel, where the rule above touches every
    // pixel: those keep the plain loop (nothing to save on them).
    const bool tiny = radius < 1.0;
    const auto exact = [&](uint32_t x, uint32_t y) {
        const double px = x + 0.5, py = y + 0.5;
        const double cx = std::clamp(px, radius, w - radius), cy = std::clamp(py, radius, h - radius);
        const double d = std::hypot(px - cx, py - cy) - radius;
        if (d <= -0.5) return;
        const double keep = d >= 0.5 ? 0.0 : 0.5 - d;
        uint8_t& a = image.rgba[(size_t(y) * w + x) * 4 + 3];
        a = uint8_t(std::lround(a * keep));
    };
    if (tiny) {
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) exact(x, y);
        return;
    }
    // Columns left of `leftCount` and from `rightStart` on are in a corner's reach.
    const int64_t width = int64_t(w);
    const int64_t leftCount = std::clamp<int64_t>(int64_t(std::ceil(radius - 0.5)), 0, width);
    const int64_t rightStart = std::clamp<int64_t>(int64_t(std::floor(double(w) - radius - 0.5)) + 1, 0, width);
    for (uint32_t y = 0; y < h; ++y) {
        const double py = y + 0.5;
        const double dy = std::abs(py - std::clamp(py, radius, h - radius));
        if (dy == 0) continue;   // between the corners: every pixel of the row is inside
        const double inside2 = (radius - 0.5) * (radius - 0.5) - dy * dy, outside2 = (radius + 0.5) * (radius + 0.5) - dy * dy;
        const double inside = inside2 > 0 ? std::sqrt(inside2) : -2.0;   // |dx| up to here: surely inside
        const double outside = outside2 > 0 ? std::sqrt(outside2) : 0.0;  // |dx| from here on: surely outside
        uint8_t* row = &image.rgba[size_t(y) * w * 4];
        // Left corner: dx = radius - (x + 0.5), falling as x grows.
        {
            const int64_t zeroEnd = std::clamp<int64_t>(int64_t(std::floor(radius - 1.5 - outside)) + 1, 0, leftCount);
            const int64_t keepStart = std::clamp<int64_t>(int64_t(std::ceil(radius + 0.5 - inside)), zeroEnd, leftCount);
            for (int64_t x = 0; x < zeroEnd; ++x) row[x * 4 + 3] = 0;
            for (int64_t x = zeroEnd; x < keepStart; ++x) exact(uint32_t(x), y);
        }
        // Right corner: dx = (x + 0.5) - (w - radius), growing with x.
        {
            const double edge = double(w) - radius;
            const int64_t zeroStart = std::clamp<int64_t>(int64_t(std::ceil(edge + outside + 0.5)), rightStart, width);
            const int64_t keepEnd = std::clamp<int64_t>(int64_t(std::floor(edge + inside - 1.5)) + 1, rightStart, zeroStart);
            for (int64_t x = keepEnd; x < zeroStart; ++x) exact(uint32_t(x), y);
            for (int64_t x = zeroStart; x < width; ++x) row[x * 4 + 3] = 0;
        }
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
