// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS streaming mode sources and extra audio without devices: the per-frame touches of a camera,
// window or video layer (mirror, rounded corners, circle) and the extra-audio input buffer.
#include "veyra/engine/PushAuxAudio.h"
#include "veyra/engine/PushLayerSources.h"
#include "veyra/engine/PushCanvas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace veyra::engine;

namespace {
int failures = 0;
void check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}
PushOverlayImage gradient(uint32_t w, uint32_t h) {
    PushOverlayImage image{w, h, {}, std::vector<uint8_t>(size_t(w) * h * 4)};
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* p = &image.rgba[(size_t(y) * w + x) * 4];
            p[0] = uint8_t(x); p[1] = uint8_t(y); p[2] = 7; p[3] = 255;
        }
    return image;
}
const uint8_t* px(const PushOverlayImage& i, uint32_t x, uint32_t y) { return &i.rgba[(size_t(y) * i.width + x) * 4]; }

// The shape as it was computed until 3.2.3: the distance for every pixel of every frame. The frame
// loop now computes only the edge; this is what it must still equal, pixel for pixel.
void shapeReference(PushOverlayImage& image, bool mirror, PushSourceShape shape) {
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
        const uint32_t side = std::min(W, H), left = (W - side) / 2, top = (H - side) / 2;
        std::vector<uint8_t> square(size_t(side) * side * 4);
        for (uint32_t y = 0; y < side; ++y)
            std::memcpy(&square[size_t(y) * side * 4], &image.rgba[(size_t(top + y) * W + left) * 4], size_t(side) * 4);
        image.rgba = std::move(square);
        image.width = image.height = side;
    }
    const uint32_t w = image.width, h = image.height;
    const double radius = shape == PushSourceShape::Circle ? w / 2.0 : std::min(w, h) * 0.08;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const double qx = x + 0.5, qy = y + 0.5;
            const double cx = std::clamp(qx, radius, w - radius), cy = std::clamp(qy, radius, h - radius);
            const double d = std::hypot(qx - cx, qy - cy) - radius;
            if (d <= -0.5) continue;
            const double keep = d >= 0.5 ? 0.0 : 0.5 - d;
            uint8_t& a = image.rgba[(size_t(y) * w + x) * 4 + 3];
            a = uint8_t(std::lround(a * keep));
        }
}
// A picture with every byte different from its neighbours, alpha included.
PushOverlayImage noisy(uint32_t w, uint32_t h) {
    PushOverlayImage image{w, h, {}, std::vector<uint8_t>(size_t(w) * h * 4)};
    uint32_t state = w * 2654435761u + h;
    for (auto& b : image.rgba) { state = state * 1664525u + 1013904223u; b = uint8_t(state >> 24); }
    return image;
}
// The camera choice as it was before 3.2.3 (automatic must still equal it).
int chooseReference(const std::vector<PushCameraFormat>& formats) {
    int best = -1;
    double bestScore = 1e18;
    for (size_t i = 0; i < formats.size(); ++i) {
        const auto& f = formats[i];
        if (!f.width || !f.height) continue;
        double score = std::abs(double(f.width) - 1280) + std::abs(double(f.height) - 720);
        if (f.width > 1920) score += 1e6;
        if (f.fps < 29) score += 1e5 + (30 - f.fps) * 100;
        if (score < bestScore) { bestScore = score; best = int(i); }
    }
    return best;
}
} // namespace

int main() {
    // Mirror: the left column becomes the right one.
    {
        auto image = gradient(10, 4);
        shapePushSourceFrame(image, true, PushSourceShape::Rectangle);
        check(px(image, 0, 0)[0] == 9 && px(image, 9, 0)[0] == 0, "mirror swaps left and right");
        check(px(image, 4, 2)[1] == 2 && px(image, 4, 2)[3] == 255, "mirror keeps rows and alpha");
    }
    // Rounded corners: transparent at the very corner, opaque in the middle and along the edges.
    {
        auto image = gradient(100, 50);
        shapePushSourceFrame(image, false, PushSourceShape::Rounded);
        check(image.width == 100 && image.height == 50, "rounded keeps the size");
        check(px(image, 0, 0)[3] == 0 && px(image, 99, 49)[3] == 0, "rounded: the corners are cut");
        check(px(image, 50, 25)[3] == 255 && px(image, 50, 0)[3] == 255 && px(image, 0, 25)[3] == 255, "rounded: middle and edges stay");
    }
    // Circle: the centred square, round.
    {
        auto image = gradient(160, 90);
        shapePushSourceFrame(image, false, PushSourceShape::Circle);
        check(image.width == 90 && image.height == 90, "circle crops to the centred square");
        check(px(image, 0, 0)[0] == 35, "circle: the square starts 35 pixels in");
        check(px(image, 45, 45)[3] == 255 && px(image, 2, 2)[3] == 0, "circle: inside opaque, corners clear");
        check(px(image, 45, 0)[3] > 100 && px(image, 0, 45)[3] > 100, "circle: touches the square's edges");
        bool soft = false;
        for (uint32_t x = 0; x < 90 && !soft; ++x) { const uint8_t a = px(image, x, 10)[3]; soft = a > 0 && a < 255; }
        check(soft, "circle: anti-aliased edge");
    }
    // Too small or malformed pictures are left alone.
    {
        PushOverlayImage tiny{1, 1, {}, std::vector<uint8_t>(4, 9)};
        shapePushSourceFrame(tiny, true, PushSourceShape::Circle);
        check(tiny.width == 1 && tiny.rgba[3] == 9, "a 1 x 1 picture is untouched");
    }

    // Extra audio buffer.
    {
        PushAuxFifo fifo(960, 4800);
        std::vector<float> in(500 * 2, 0.5f), out(480 * 2, 0.0f);
        fifo.push(in.data(), 500);
        check(!fifo.mixInto(out.data(), 480, 1.0f) && out[0] == 0.0f, "silence until the prebuffer is in");
        fifo.push(in.data(), 500);
        check(fifo.mixInto(out.data(), 480, 1.0f) && std::abs(out[0] - 0.5f) < 1e-6f, "plays once primed");
        std::fill(out.begin(), out.end(), 0.25f);
        check(fifo.mixInto(out.data(), 480, 0.5f) && std::abs(out[10] - 0.5f) < 1e-6f, "adds with its gain");
        check(fifo.frames() == 40, "keeps what is left");
        std::fill(out.begin(), out.end(), 0.0f);
        check(!fifo.mixInto(out.data(), 480, 1.0f) && fifo.underruns() == 1, "running dry counts an underrun");
        check(std::abs(out[0] - 0.5f) < 1e-6f && out[100] == 0.0f, "the rest plays, then silence");
        check(fifo.frames() == 0 && !fifo.mixInto(out.data(), 480, 1.0f), "refills before it plays again");
    }
    {
        PushAuxFifo fifo(10, 100);
        std::vector<float> in(300 * 2);
        for (size_t i = 0; i < 300; ++i) in[i * 2] = in[i * 2 + 1] = float(i);
        fifo.push(in.data(), 300);
        check(fifo.frames() == 100 && fifo.dropped() == 200, "never holds more than its limit");
        std::vector<float> out(2, 0.0f);
        fifo.mixInto(out.data(), 1, 1.0f);
        check(out[0] == 200.0f, "the oldest samples go first (no delay builds up)");
        std::vector<float> bad(2, std::nanf(""));
        fifo.push(bad.data(), 1);
        check(fifo.frames() == 100, "a sample that is not a number becomes silence, not a crash");
    }
    // Source crop.
    {
        auto image = gradient(100, 50);
        cropPushSourceFrame(image, 0.1f, 0.2f, 0.3f, 0.0f);
        check(image.width == 60 && image.height == 40, "crop cuts each side");
        check(px(image, 0, 0)[0] == 10 && px(image, 0, 0)[1] == 10, "crop starts at the cut corner");
        auto same = gradient(20, 20);
        cropPushSourceFrame(same, 0, 0, 0, 0);
        check(same.width == 20, "no crop leaves the picture");
        auto greedy = gradient(20, 20);
        cropPushSourceFrame(greedy, 0.9f, 0.9f, 0.9f, 0.9f);
        check(greedy.width >= 2 && greedy.height >= 2 && greedy.width <= 20, "a crop never leaves less than 2 x 2");
    }
    // Scene pictures on another shape: bars, never stretched.
    {
        PushOverlayImage card{160, 90, {}, std::vector<uint8_t>(size_t(160) * 90 * 4, 255)};
        PushOverlayImage out;
        check(padPushOverlayToShape(card, 1080, 1920, out), "a 16:9 card on a 9:16 output is padded");
        check(out.width == 160 && out.height == 284, "padded to 9:16 at its own width");
        check(out.rgba[3] == 255 && out.rgba[0] == 0, "opaque card: black bars that still cover");
        check(px(out, 80, 142)[0] == 255, "the card in the middle");
        PushOverlayImage clear{160, 90, {}, std::vector<uint8_t>(size_t(160) * 90 * 4, 0)};
        check(padPushOverlayToShape(clear, 1080, 1920, out) && out.rgba[3] == 0, "a see-through scene gets clear bars");
        check(!padPushOverlayToShape(card, 1920, 1080, out), "the same shape is left alone");
    }
    // Canvas and output sizes (PushCanvas.h).
    {
        uint32_t w = 0, h = 0;
        pushOutputSize(0, 0, 3840, 2160, 8192, w, h);
        check(w == 3840 && h == 2160, "0 x 0 keeps the picture's size");
        pushOutputSize(1920, 1080, 3840, 2160, 4096, w, h);
        check(w == 1920 && h == 1080, "a requested size is used");
        pushOutputSize(0, 720, 3840, 2160, 4096, w, h);
        check(w == 1280 && h == 720, "one side 0 keeps the shape");
        pushOutputSize(7680, 4320, 7680, 4320, 4096, w, h);
        check(w == 4096 && h == 2304, "never above the limit, shape kept");
        pushOutputSize(1665, 937, 1920, 1080, 4096, w, h);
        check(w == 1664 && h == 936, "sizes are even");
        // An odd picture (a window of odd width) is recorded at the even size next to it, through the canvas.
        pushOutputSize(0, 0, 1919, 1081, 8192, w, h);
        check(w == 1918 && h == 1080, "an odd picture gets an even output size");
        PushCanvas full;
        check(full.fullFrame(), "the default canvas is the whole picture");
        auto m = mapPushCanvas(full, 3840, 2160, 1920, 1080);
        check(m.srcW == 3840 && m.dstW == 1920 && m.dstX == 0 && m.dstY == 0, "16:9 on 16:9: the whole picture fills the output");
        // 16:9 game on a 9:16 output.
        PushCanvas fit; fit.fit = PushCanvasFit::Fit;
        m = mapPushCanvas(fit, 1920, 1080, 1080, 1920);
        check(std::abs(m.dstW - 1080) < 1e-6 && std::abs(m.dstH - 607.5) < 1e-6 && std::abs(m.dstY - 656.25) < 1e-6, "fit: the whole picture, bars above and below");
        PushCanvas fill; fill.fit = PushCanvasFit::Fill;
        m = mapPushCanvas(fill, 1920, 1080, 1080, 1920);
        check(m.dstW == 1080 && m.dstH == 1920 && std::abs(m.srcW - 607.5) < 1e-6 && std::abs(m.srcX - 656.25) < 1e-6 && std::abs(m.srcH - 1080) < 1e-6,
              "fill: the middle of the picture covers the output");
        PushCanvas box; box.x = 0.5f; box.y = 0.0f; box.w = 0.5f; box.h = 0.5f; box.fit = PushCanvasFit::Stretch;
        box.cropLeft = 0.25f;
        m = mapPushCanvas(box, 1920, 1080, 1920, 1080);
        check(m.dstX == 960 && m.dstW == 960 && m.dstH == 540 && m.srcX == 480 && m.srcW == 1440, "a box and a crop");
        check(!box.fullFrame(), "a box is not the whole picture");
        PushCanvas bad; bad.w = std::nanf(""); bad.cropTop = 2.0f;
        check(bad.sanitized().w == 1.0f && bad.sanitized().cropTop == 0.45f, "bad numbers are cleaned");
        check(pushCanvasTaps(3840, 1920) == 2 && pushCanvasTaps(1920, 3840) == 1 && pushCanvasTaps(7680, 640) == 4, "box filter taps follow the shrink");
    }
    // 3.2.3: the shape computes only the edge. It must equal the every-pixel version exactly, at
    // every size a camera, a window or a video can have, odd ones and tiny ones included.
    {
        const std::pair<uint32_t, uint32_t> sizes[] = {{2, 2}, {3, 5}, {5, 3}, {6, 6}, {7, 7}, {8, 13}, {13, 8}, {16, 9}, {31, 17}, {64, 64}, {65, 65},
                                                       {101, 57}, {320, 180}, {641, 361}, {1280, 720}, {1920, 1080}, {1080, 1920}, {1728, 3072}};
        bool same = true;
        std::string first;
        for (const auto& [w, h] : sizes)
            for (const auto shape : {PushSourceShape::Rectangle, PushSourceShape::Rounded, PushSourceShape::Circle})
                for (const bool mirror : {false, true}) {
                    auto fast = noisy(w, h), reference = fast;
                    shapePushSourceFrame(fast, mirror, shape);
                    shapeReference(reference, mirror, shape);
                    if (fast.width != reference.width || fast.height != reference.height || fast.rgba != reference.rgba) {
                        if (same) first = std::to_string(w) + "x" + std::to_string(h) + " shape " + std::to_string(int(shape)) + (mirror ? " mirrored" : "");
                        same = false;
                    }
                }
        check(same, "the shape equals the every-pixel version at every size" + (same ? std::string() : " (first difference: " + first + ")"));
        // And it really leaves the inside alone: a rounded 1080p frame changes only its corners.
        auto frame = noisy(1920, 1080);
        const auto before = frame.rgba;
        shapePushSourceFrame(frame, false, PushSourceShape::Rounded);
        size_t changed = 0;
        for (size_t i = 3; i < before.size(); i += 4) changed += before[i] != frame.rgba[i];
        check(changed > 1000 && changed < 12000, "rounded corners touch a few thousand pixels of two million");
    }
    // RGB32 from a camera or a window into RGBA: red and blue change places, alpha is opaque.
    {
        const uint32_t w = 37, h = 11;
        const ptrdiff_t pitch = ptrdiff_t(w) * 4 + 12;   // a row padded as capture buffers are
        std::vector<uint8_t> source(size_t(pitch) * h);
        uint32_t state = 99;
        for (auto& b : source) { state = state * 1664525u + 1013904223u; b = uint8_t(state >> 24); }
        PushOverlayImage out;
        pushBgrxToRgba(source.data(), pitch, w, h, 0, out);
        bool right = out.width == w && out.height == h && out.rgba.size() == size_t(w) * h * 4;
        for (uint32_t y = 0; right && y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                const uint8_t* s = &source[size_t(y) * pitch + x * 4];
                const uint8_t* d = px(out, x, y);
                right = right && d[0] == s[2] && d[1] == s[1] && d[2] == s[0] && d[3] == 255;
            }
        check(right, "RGB32 rows become RGBA with opaque alpha");
        // Bottom-up rows (a negative pitch from the last row).
        PushOverlayImage flipped;
        pushBgrxToRgba(source.data() + size_t(pitch) * (h - 1), -pitch, w, h, 0, flipped);
        bool upside = flipped.width == w && flipped.height == h;
        for (uint32_t y = 0; upside && y < h; ++y)
            upside = std::memcmp(px(flipped, 0, y), px(out, 0, h - 1 - y), size_t(w) * 4) == 0;
        check(upside, "bottom-up rows are read from the last one");
        PushOverlayImage none;
        pushBgrxToRgba(nullptr, 4, 8, 8, 0, none);
        check(none.width == 0 && none.rgba.empty(), "no picture, no frame");
    }
    // A frame larger than the layer is shown is averaged down while it is converted.
    {
        // 8 x 4 in four flat 4 x 2 quarters -> 2 x 2... as 4 x 2: each output pixel one 2 x 2 box.
        const uint32_t w = 8, h = 4;
        std::vector<uint8_t> source(size_t(w) * h * 4);
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                uint8_t* s = &source[(size_t(y) * w + x) * 4];
                s[0] = uint8_t(10 * (x / 2));            // blue: flat per box
                s[1] = uint8_t(x % 2 ? 100 : 50);        // green: 50 and 100 in every box
                s[2] = uint8_t(y % 2 ? 201 : 200);       // red: 200 and 201 in every box
                s[3] = 0;
            }
        PushOverlayImage out;
        pushBgrxToRgba(source.data(), ptrdiff_t(w) * 4, w, h, 4, out);
        check(out.width == 4 && out.height == 2, "a frame is fitted to the width the layer is shown");
        bool averaged = out.rgba.size() == 4 * 2 * 4;
        for (uint32_t y = 0; averaged && y < 2; ++y)
            for (uint32_t x = 0; x < 4; ++x) {
                const uint8_t* d = px(out, x, y);
                averaged = averaged && d[0] == 201 && d[1] == 75 && d[2] == 10 * x && d[3] == 255;   // 200.5 rounds up
            }
        check(averaged, "each pixel is the average of its box, red and blue in place");
        // The same from RGBA, for a frame cropped first.
        auto image = gradient(64, 36);
        fitPushSourceFrame(image, 16);
        check(image.width == 16 && image.height == 9 && image.rgba.size() == 16 * 9 * 4, "an RGBA frame is fitted the same way");
        check(px(image, 0, 0)[0] == 2 && px(image, 15, 0)[0] == 62 && px(image, 0, 8)[1] == 34 && px(image, 3, 3)[3] == 255,
              "its boxes are averaged (columns 0-3 give 1.5, rounded up)");
        auto flat = gradient(64, 36);
        for (size_t i = 0; i < flat.rgba.size(); i += 4) { flat.rgba[i] = 90; flat.rgba[i + 1] = 91; flat.rgba[i + 2] = 92; flat.rgba[i + 3] = 255; }
        fitPushSourceFrame(flat, 23);   // not a whole ratio
        bool even = flat.width == 23 && flat.height == 13;
        for (size_t i = 0; even && i < flat.rgba.size(); i += 4) even = flat.rgba[i] == 90 && flat.rgba[i + 1] == 91 && flat.rgba[i + 2] == 92 && flat.rgba[i + 3] == 255;
        check(even, "a flat picture stays flat at any ratio");
        auto narrow = gradient(20, 10);   // (never the name `small`: Windows defines it)
        fitPushSourceFrame(narrow, 40);
        check(narrow.width == 20 && narrow.height == 10, "a frame is never enlarged");
        // How wide to fit: as wide as it is shown, more for a circle (it shows the centred square).
        check(pushSourceFitWidth(1920, 1080, 480, PushSourceShape::Rounded) == 480, "a corner camera is fitted to its corner");
        check(pushSourceFitWidth(1920, 1080, 480, PushSourceShape::Circle) == 854, "a circle keeps enough for its square");
        check(pushSourceFitWidth(1080, 1920, 480, PushSourceShape::Circle) == 480, "an upright frame's square is its width");
        check(pushSourceFitWidth(1920, 1080, 1600, PushSourceShape::Rectangle) == 1920, "a little too large is left alone (resampling only blurs)");
        check(pushSourceFitWidth(3840, 2160, 1920, PushSourceShape::Rectangle) == 1920, "a 4K camera shown full width on 1080p");
        check(pushSourceFitWidth(1280, 720, 0, PushSourceShape::Rectangle) == 1280 && pushSourceFitWidth(1280, 720, 4000, PushSourceShape::Rectangle) == 1280, "no limit, or a larger one: unchanged");
        // How wide a layer is shown: its share of the picture, in steps, never assuming less than 1080p.
        check(pushSourceShownWidth(0.24f, 1920) == 480 && pushSourceShownWidth(0.24f, 0) == 480 && pushSourceShownWidth(0.24f, 1280) == 480, "24 % of a 1080p picture (or an unknown one)");
        check(pushSourceShownWidth(0.24f, 3840) == 960 && pushSourceShownWidth(1.0f, 3840) == 3840 && pushSourceShownWidth(1.0f, 1920) == 1920, "of a 4K picture, and the whole width");
        check(pushSourceShownWidth(0.05f, 1920) == 320 && pushSourceShownWidth(0.25f, 1920) == 480 && pushSourceShownWidth(0.26f, 1920) == 640, "at least 320, in steps of 160");
        check(pushSourceShownWidth(0.0f, 1920) == 0 && pushSourceShownWidth(std::nanf(""), 1920) == 0 && pushSourceShownWidth(3.0f, 1920) == 1920, "no share: no limit; more than all of it: all of it");
    }
    // Which format a camera is opened with.
    {
        const uint32_t mjpg = 0x47504A4D, nv12 = 0x3231564E;
        // A 4K webcam: 4K only as MJPEG, smaller sizes at 60.
        const std::vector<PushCameraFormat> uhd = {{3840, 2160, 30, mjpg}, {1920, 1080, 60, mjpg}, {1920, 1080, 30, nv12}, {1280, 720, 60, mjpg}, {640, 480, 30, nv12}};
        // A pocket camera in webcam mode: 1080p at 24/25/30 and an upright 3K, nothing larger.
        const std::vector<PushCameraFormat> pocket = {{1920, 1080, 24, mjpg}, {1920, 1080, 25, mjpg}, {1920, 1080, 30, mjpg},
                                                      {1728, 3072, 24, mjpg}, {1728, 3072, 30, mjpg}, {1728, 3072, 60, mjpg}};
        // A camera on a slow port: 4K at 15 only.
        const std::vector<PushCameraFormat> slow = {{3840, 2160, 15, mjpg}, {1920, 1080, 30, mjpg}, {1280, 720, 30, mjpg}};
        const std::vector<PushCameraFormat> cinema = {{3840, 2160, 25, mjpg}, {1920, 1080, 25, mjpg}};
        bool unchanged = true;
        for (const auto* list : {&uhd, &pocket, &slow, &cinema}) unchanged = unchanged && choosePushCameraFormat(*list, 0) == chooseReference(*list);
        check(unchanged, "automatic picks what it always picked");
        check(choosePushCameraFormat(uhd, 0) == 3 && choosePushCameraFormat(pocket, 0) == 2, "automatic: 720p, or the smallest 30 FPS size");
        check(choosePushCameraFormat(uhd, 2160) == 0, "4K asked and offered: 4K");
        check(choosePushCameraFormat(uhd, 1080) == 1 && choosePushCameraFormat(uhd, 1440) == 1, "1080p asked: 1080p; 1440p asked without one: the closest");
        check(choosePushCameraFormat(pocket, 2160) == 2 && choosePushCameraFormat(pocket, 1080) == 2, "a camera that sends 1080p at most gives 1080p, at 30 rather than 24");
        check(choosePushCameraFormat(slow, 2160) == 1, "4K at 15 FPS loses to 1080p at 30");
        check(choosePushCameraFormat(cinema, 2160) == 0, "4K at 25 FPS counts");
        check(choosePushCameraFormat({}, 0) == -1 && choosePushCameraFormat({{0, 0, 30, 0}}, 1080) == -1, "nothing offered, nothing chosen");
        const auto sizes = pushCameraSizes(uhd);
        check(sizes.size() == 4 && sizes[0].width == 3840 && sizes[1].width == 1920 && sizes[1].fps == 60 && sizes[3].width == 640, "the sizes a camera offers, largest first, each once");
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
