// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS streaming mode sources and extra audio without devices: the per-frame touches of a camera,
// window or video layer (mirror, rounded corners, circle) and the extra-audio input buffer.
#include "veyra/engine/PushAuxAudio.h"
#include "veyra/engine/PushLayerSources.h"

#include <cmath>
#include <cstdio>
#include <string>
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
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
