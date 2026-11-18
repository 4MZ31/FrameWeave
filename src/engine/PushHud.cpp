// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#include "veyra/engine/PushHud.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace veyra::engine::hud {
namespace {

float halfToFloat(uint16_t h) {
    const uint32_t sign = uint32_t(h >> 15) << 31;
    uint32_t exponent = (h >> 10) & 0x1F, mantissa = h & 0x3FF, bits = 0;
    if (exponent == 0) {
        if (mantissa == 0) bits = sign;
        else { // subnormal
            exponent = 127 - 15 + 1;
            while (!(mantissa & 0x400)) { mantissa <<= 1; --exponent; }
            mantissa &= 0x3FF;
            bits = sign | (exponent << 23) | (mantissa << 13);
        }
    } else if (exponent == 31) bits = sign | 0x7F800000 | (mantissa << 13);
    else bits = sign | ((exponent - 15 + 127) << 23) | (mantissa << 13);
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

struct Rgb { float r, g, b; }; // 0..255

Rgb pixel(const uint8_t* row, uint32_t x, PixelLayout layout) {
    switch (layout) {
    case PixelLayout::Rgba8: { const uint8_t* p = row + size_t(x) * 4; return {float(p[0]), float(p[1]), float(p[2])}; }
    case PixelLayout::Bgra8: { const uint8_t* p = row + size_t(x) * 4; return {float(p[2]), float(p[1]), float(p[0])}; }
    case PixelLayout::Rgb10a2: {
        uint32_t v; std::memcpy(&v, row + size_t(x) * 4, 4);
        return {float((v & 0x3FF) >> 2), float(((v >> 10) & 0x3FF) >> 2), float(((v >> 20) & 0x3FF) >> 2)};
    }
    case PixelLayout::Rgba16f: {
        uint16_t c[3]; std::memcpy(c, row + size_t(x) * 8, 6);
        const auto encode = [](float linear) {
            if (!std::isfinite(linear)) return 0.0f;
            return std::sqrt(std::clamp(linear, 0.0f, 1.0f)) * 255.0f;
        };
        return {encode(halfToFloat(c[0])), encode(halfToFloat(c[1])), encode(halfToFloat(c[2]))};
    }
    }
    return {0, 0, 0};
}

// Luma of one pixel, 0..255, without building the RGB triple first (reduce() reads up to nine
// samples for each of two million grid points). FP16: one square root for the pixel.
float lumaAt(const uint8_t* row, uint32_t x, PixelLayout layout) {
    switch (layout) {
    case PixelLayout::Rgba8: { const uint8_t* p = row + size_t(x) * 4; return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]; }
    case PixelLayout::Bgra8: { const uint8_t* p = row + size_t(x) * 4; return 0.2126f * p[2] + 0.7152f * p[1] + 0.0722f * p[0]; }
    case PixelLayout::Rgb10a2: {
        uint32_t v; std::memcpy(&v, row + size_t(x) * 4, 4);
        return 0.2126f * float((v & 0x3FF) >> 2) + 0.7152f * float(((v >> 10) & 0x3FF) >> 2) + 0.0722f * float(((v >> 20) & 0x3FF) >> 2);
    }
    case PixelLayout::Rgba16f: {
        uint16_t c[3]; std::memcpy(c, row + size_t(x) * 8, 6);
        const auto channel = [](uint16_t h) { const float f = halfToFloat(h); return std::isfinite(f) ? std::clamp(f, 0.0f, 1.0f) : 0.0f; };
        return std::sqrt(0.2126f * channel(c[0]) + 0.7152f * channel(c[1]) + 0.0722f * channel(c[2])) * 255.0f;
    }
    }
    return 0;
}

// Inclusive cell bounds, the candidate cells inside, and the evidence's own extent in grid points.
struct Box { uint32_t x0, y0, x1, y1; uint32_t cells; uint32_t px0, py0, px1, py1; };

uint32_t area(const Box& b) { return (b.x1 - b.x0 + 1) * (b.y1 - b.y0 + 1); }

} // namespace

bool reduce(const uint8_t* data, size_t pitch, uint32_t width, uint32_t height, PixelLayout layout,
            LumaFrame& out, std::vector<uint8_t>* previewRgb) {
    if (!data || width < kGridWidth / 8 || height < kGridHeight / 8) return false;
    const size_t bpp = layout == PixelLayout::Rgba16f ? 8 : 4;
    if (pitch < size_t(width) * bpp) return false;
    out.luma.assign(size_t(kGridWidth) * kGridHeight, 0);
    // One grid point per source pixel up to 1920x1080; larger pictures average 2x2 or 3x3 samples
    // per point, so a stroke a few pixels wide stays a point of its own colour.
    const int samplesX = int(std::clamp<uint32_t>((width + kGridWidth - 1) / kGridWidth, 1, 3));
    const int samplesY = int(std::clamp<uint32_t>((height + kGridHeight - 1) / kGridHeight, 1, 3));
    std::vector<uint32_t> columns(size_t(kGridWidth) * size_t(samplesX));
    for (uint32_t gx = 0; gx < kGridWidth; ++gx) {
        const double x0 = double(gx) * width / kGridWidth, x1 = double(gx + 1) * width / kGridWidth;
        for (int sx = 0; sx < samplesX; ++sx)
            columns[size_t(gx) * size_t(samplesX) + size_t(sx)] = std::min<uint32_t>(width - 1, uint32_t(x0 + (x1 - x0) * (2 * sx + 1) / (2.0 * samplesX)));
    }
    const float scale = 1.0f / float(samplesX * samplesY);
    for (uint32_t gy = 0; gy < kGridHeight; ++gy) {
        const double y0 = double(gy) * height / kGridHeight, y1 = double(gy + 1) * height / kGridHeight;
        const uint8_t* rows[3]{};
        for (int sy = 0; sy < samplesY; ++sy)
            rows[sy] = data + size_t(std::min<uint32_t>(height - 1, uint32_t(y0 + (y1 - y0) * (2 * sy + 1) / (2.0 * samplesY)))) * pitch;
        uint8_t* line = out.luma.data() + size_t(gy) * kGridWidth;
        for (uint32_t gx = 0; gx < kGridWidth; ++gx) {
            float sum = 0;
            const uint32_t* cols = columns.data() + size_t(gx) * size_t(samplesX);
            for (int sy = 0; sy < samplesY; ++sy)
                for (int sx = 0; sx < samplesX; ++sx) sum += lumaAt(rows[sy], cols[sx], layout);
            line[gx] = uint8_t(std::clamp(std::lround(sum * scale), 0L, 255L));
        }
    }
    if (previewRgb) {
        previewRgb->assign(size_t(kPreviewWidth) * kPreviewHeight * 3, 0);
        for (uint32_t py = 0; py < kPreviewHeight; ++py) {
            const uint32_t y = std::min<uint32_t>(height - 1, uint32_t((py + 0.5) * height / kPreviewHeight));
            const uint8_t* row = data + size_t(y) * pitch;
            for (uint32_t px = 0; px < kPreviewWidth; ++px) {
                const uint32_t x = std::min<uint32_t>(width - 1, uint32_t((px + 0.5) * width / kPreviewWidth));
                const Rgb c = pixel(row, x, layout);
                uint8_t* o = previewRgb->data() + (size_t(py) * kPreviewWidth + px) * 3;
                o[0] = uint8_t(std::clamp(c.r, 0.0f, 255.0f));
                o[1] = uint8_t(std::clamp(c.g, 0.0f, 255.0f));
                o[2] = uint8_t(std::clamp(c.b, 0.0f, 255.0f));
            }
        }
    }
    return true;
}

Detection detect(const std::vector<LumaFrame>& frames, size_t maxRegions) {
    Detection result;
    const size_t points = size_t(kGridWidth) * kGridHeight;
    if (frames.size() < kMinFrames || maxRegions == 0) return result;
    for (const auto& f : frames) if (f.luma.size() != points) return result;

    // Per grid point: mean and standard deviation over time.
    std::vector<float> mean(points, 0), deviation(points, 0);
    const float n = float(frames.size());
    for (const auto& f : frames)
        for (size_t i = 0; i < points; ++i) mean[i] += f.luma[i];
    for (auto& m : mean) m /= n;
    for (const auto& f : frames)
        for (size_t i = 0; i < points; ++i) { const float d = f.luma[i] - mean[i]; deviation[i] += d * d; }
    for (auto& d : deviation) d = std::sqrt(d / n);

    // The picture's typical motion: the median over the decision cells of the largest deviation inside.
    const size_t cells = size_t(kCellsX) * kCellsY;
    std::vector<float> cellDeviation(cells, 0);
    for (uint32_t y = 0; y < kGridHeight; ++y)
        for (uint32_t x = 0; x < kGridWidth; ++x) {
            float& c = cellDeviation[size_t(y / kCell) * kCellsX + x / kCell];
            c = std::max(c, deviation[size_t(y) * kGridWidth + x]);
        }
    std::vector<float> sorted = cellDeviation;
    std::nth_element(sorted.begin(), sorted.begin() + std::ptrdiff_t(sorted.size() / 2), sorted.end());
    result.motion = sorted[sorted.size() / 2];
    if (result.motion < 1.5f) { result.tooStill = true; return result; }

    // Static: near capture or stream noise and well below the picture's typical motion. The softer
    // bound also takes strokes that let a little of the scene through (not fully opaque HUD text).
    const float stillBelow = std::clamp(0.3f * result.motion, 1.5f, 4.0f);
    const float softBelow = std::clamp(0.3f * result.motion, stillBelow, 9.0f);
    const float movingAbove = std::max(2.0f * softBelow, 6.0f);

    // Summed-area tables of the soft-static and the moving points, for the neighbourhood tests below.
    const uint32_t tw = kGridWidth + 1;
    std::vector<uint32_t> staticSum(size_t(tw) * (kGridHeight + 1), 0), movingSum(size_t(tw) * (kGridHeight + 1), 0);
    for (uint32_t y = 0; y < kGridHeight; ++y) {
        uint32_t rowStatic = 0, rowMoving = 0;
        for (uint32_t x = 0; x < kGridWidth; ++x) {
            const float d = deviation[size_t(y) * kGridWidth + x];
            rowStatic += d <= softBelow; rowMoving += d >= movingAbove;
            staticSum[size_t(y + 1) * tw + x + 1] = staticSum[size_t(y) * tw + x + 1] + rowStatic;
            movingSum[size_t(y + 1) * tw + x + 1] = movingSum[size_t(y) * tw + x + 1] + rowMoving;
        }
    }
    const auto boxSum = [&](const std::vector<uint32_t>& t, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {   // [x0, x1) x [y0, y1)
        return t[size_t(y1) * tw + x1] - t[size_t(y0) * tw + x1] - t[size_t(y1) * tw + x0] + t[size_t(y0) * tw + x0];
    };

    // Evidence that a point belongs to the HUD, either of:
    //  - contrast: a luma step of the mean picture between two neighbouring points that both stay
    //    still (text and icons on an opaque panel keep their edges; the edge between a flat static
    //    bar and the moving scene does not count, since one side of it moves);
    //  - a thin still shape in a moving surround: a still point whose neighbourhood is mostly moving
    //    (text drawn straight over the game, thin bars, the ring of a minimap). The edge of a large
    //    still area (letterbox bars, a flat sky) has more than half of its neighbourhood still and
    //    does not count.
    constexpr float kDetail = 6.0f;
    constexpr int kReach = 12;                 // neighbourhood half size, grid points
    constexpr float kThinShare = 0.45f, kMovingShare = 0.35f;
    std::vector<uint8_t> evidence(points, 0);
    for (uint32_t y = 0; y < kGridHeight; ++y)
        for (uint32_t x = 0; x < kGridWidth; ++x) {
            const size_t i = size_t(y) * kGridWidth + x;
            if (deviation[i] > softBelow) continue;
            // Contrast between two still neighbours; against a neighbour that is only nearly still
            // (a slowly changing sky behind the text) the step has to be twice as large, and only
            // the still point counts.
            const auto step = [&](size_t j) {
                if (deviation[j] > softBelow) return;
                const bool iStill = deviation[i] <= stillBelow, jStill = deviation[j] <= stillBelow;
                if (!iStill && !jStill) return;
                if (std::fabs(mean[i] - mean[j]) < (iStill && jStill ? kDetail : 2 * kDetail)) return;
                if (iStill) evidence[i] = 1;
                if (jStill) evidence[j] = 1;
            };
            if (x + 1 < kGridWidth) step(i + 1);
            if (y + 1 < kGridHeight) step(i + kGridWidth);
            // The thin-shape test needs a point that really holds: with the softer bound, points of a
            // moving scene that happened to vary little over a few frames passed as strokes.
            if (evidence[i] || deviation[i] > stillBelow) continue;
            const uint32_t x0 = x >= uint32_t(kReach) ? x - kReach : 0, y0 = y >= uint32_t(kReach) ? y - kReach : 0;
            const uint32_t x1 = std::min(kGridWidth, x + kReach + 1), y1 = std::min(kGridHeight, y + kReach + 1);
            const float windowArea = float((x1 - x0) * (y1 - y0));
            if (float(boxSum(staticSum, x0, y0, x1, y1)) <= kThinShare * windowArea &&
                float(boxSum(movingSum, x0, y0, x1, y1)) >= kMovingShare * windowArea) evidence[i] = 1;
        }

    // Decision cells: enough evidence points, with the evidence's own extent kept for the final box.
    struct Extent { uint16_t x0 = 0xFFFF, y0 = 0xFFFF, x1 = 0, y1 = 0; };
    std::vector<uint16_t> count(cells, 0);
    std::vector<Extent> extent(cells);
    for (uint32_t y = 0; y < kGridHeight; ++y)
        for (uint32_t x = 0; x < kGridWidth; ++x) {
            if (!evidence[size_t(y) * kGridWidth + x]) continue;
            const size_t c = size_t(y / kCell) * kCellsX + x / kCell;
            ++count[c];
            auto& e = extent[c];
            e.x0 = std::min<uint16_t>(e.x0, uint16_t(x)); e.x1 = std::max<uint16_t>(e.x1, uint16_t(x));
            e.y0 = std::min<uint16_t>(e.y0, uint16_t(y)); e.y1 = std::max<uint16_t>(e.y1, uint16_t(y));
        }
    constexpr uint16_t kCellEvidence = uint16_t(kCell * kCell / 20);   // 5% of the cell's points
    std::vector<uint8_t> candidate(cells, 0);
    for (size_t i = 0; i < cells; ++i)
        if (count[i] >= kCellEvidence) { candidate[i] = 1; ++result.candidateCells; }

    // Close gaps between letters and icon parts (3x3 dilation), then group.
    std::vector<uint8_t> grown(cells, 0);
    for (uint32_t cy = 0; cy < kCellsY; ++cy)
        for (uint32_t cx = 0; cx < kCellsX; ++cx) {
            if (!candidate[size_t(cy) * kCellsX + cx]) continue;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = int(cx) + dx, y = int(cy) + dy;
                    if (x >= 0 && y >= 0 && x < int(kCellsX) && y < int(kCellsY)) grown[size_t(y) * kCellsX + size_t(x)] = 1;
                }
        }
    std::vector<int> label(cells, -1);
    std::vector<Box> boxes;
    std::vector<uint32_t> stack;
    for (uint32_t start = 0; start < cells; ++start) {
        if (!grown[start] || label[start] >= 0) continue;
        Box box{start % kCellsX, start / kCellsX, start % kCellsX, start / kCellsX, 0, kGridWidth, kGridHeight, 0, 0};
        const int id = int(boxes.size());
        stack.assign(1, start);
        label[start] = id;
        while (!stack.empty()) {
            const uint32_t i = stack.back(); stack.pop_back();
            const uint32_t x = i % kCellsX, y = i / kCellsX;
            if (candidate[i]) {
                // The box follows the candidate cells, not the dilated ring around them.
                if (!box.cells) { box.x0 = box.x1 = x; box.y0 = box.y1 = y; }
                box.x0 = std::min(box.x0, x); box.x1 = std::max(box.x1, x);
                box.y0 = std::min(box.y0, y); box.y1 = std::max(box.y1, y);
                ++box.cells;
                const auto& e = extent[i];
                box.px0 = std::min<uint32_t>(box.px0, e.x0); box.px1 = std::max<uint32_t>(box.px1, e.x1);
                box.py0 = std::min<uint32_t>(box.py0, e.y0); box.py1 = std::max<uint32_t>(box.py1, e.y1);
            }
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = int(x) + dx, ny = int(y) + dy;
                    if (nx < 0 || ny < 0 || nx >= int(kCellsX) || ny >= int(kCellsY)) continue;
                    const uint32_t j = uint32_t(ny) * kCellsX + uint32_t(nx);
                    if (grown[j] && label[j] < 0) { label[j] = id; stack.push_back(j); }
                }
        }
        if (box.cells) boxes.push_back(box);
    }

    // A HUD element covers a modest part of the picture and is reasonably dense; a whole static half
    // (a still scene part) is not one, and neither is a line along a whole side of the picture
    // (the edge of a bar the neighbourhood test let through).
    const uint32_t maxArea = uint32_t(cells / 5);
    std::vector<Box> kept;
    for (const auto& b : boxes) {
        const uint32_t w = b.x1 - b.x0 + 1, h = b.y1 - b.y0 + 1;
        const bool sideLine = (w * 10 >= kCellsX * 9 && h <= 3) || (h * 10 >= kCellsY * 9 && w <= 3);
        if (b.cells >= 4 && area(b) <= maxArea && float(b.cells) / float(area(b)) >= 0.12f && !sideLine) kept.push_back(b);
    }

    // Merge boxes closer than three cells (a health bar next to its icon).
    for (bool merged = true; merged;) {
        merged = false;
        for (size_t a = 0; a < kept.size() && !merged; ++a)
            for (size_t b = a + 1; b < kept.size() && !merged; ++b) {
                const auto& p = kept[a];
                const auto& q = kept[b];
                const int gapX = std::max(int(q.x0) - int(p.x1), int(p.x0) - int(q.x1)) - 1;
                const int gapY = std::max(int(q.y0) - int(p.y1), int(p.y0) - int(q.y1)) - 1;
                if (std::max(gapX, gapY) > 3) continue;
                const Box u{std::min(p.x0, q.x0), std::min(p.y0, q.y0), std::max(p.x1, q.x1), std::max(p.y1, q.y1), p.cells + q.cells,
                            std::min(p.px0, q.px0), std::min(p.py0, q.py0), std::max(p.px1, q.px1), std::max(p.py1, q.py1)};
                if (area(u) > maxArea) continue;
                kept[a] = u;
                kept.erase(kept.begin() + std::ptrdiff_t(b));
                merged = true;
            }
    }
    std::sort(kept.begin(), kept.end(), [](const Box& a, const Box& b) { return a.cells > b.cells; });
    if (kept.size() > maxRegions) kept.resize(maxRegions);
    // The evidence's own extent plus a small margin (glows and drop shadows around text).
    constexpr uint32_t kMargin = 6;
    for (const auto& b : kept) {
        const uint32_t x0 = b.px0 > kMargin ? b.px0 - kMargin : 0, y0 = b.py0 > kMargin ? b.py0 - kMargin : 0;
        const uint32_t x1 = std::min(kGridWidth, b.px1 + 1 + kMargin), y1 = std::min(kGridHeight, b.py1 + 1 + kMargin);
        result.regions.push_back({float(x0) / kGridWidth, float(y0) / kGridHeight, float(x1) / kGridWidth, float(y1) / kGridHeight});
    }
    return result;
}

} // namespace veyra::engine::hud
