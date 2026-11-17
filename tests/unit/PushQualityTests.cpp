// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS ground-truth frame-generation quality test, pure part: luma grids from capture and output
// formats, PSNR, the straight-line tone fit, the blend baseline, HUD boxes, timing and the score.
#include "veyra/engine/PushQuality.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace veyra::engine::quality;
namespace {
int failures = 0;
void check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}
// A horizontal ramp moving right by `shift` pixels, as 8-bit full-range luma values.
std::vector<uint8_t> ramp(uint32_t w, uint32_t h, int shift) {
    std::vector<uint8_t> y(size_t(w) * h);
    for (uint32_t r = 0; r < h; ++r) for (uint32_t x = 0; x < w; ++x) y[size_t(r) * w + x] = uint8_t(((x + shift) * 3) % 256 / 2 + 40);
    return y;
}
Luma fromY(const std::vector<uint8_t>& y, uint32_t w, uint32_t h, bool limited = false) {
    Luma out; SourcePlanes s; s.format = SourceFormat::Nv12; s.plane0 = y.data(); s.pitch0 = w; s.width = w; s.height = h; s.limitedRange = limited;
    lumaFromSource(s, out); return out;
}
uint16_t toHalf(float f) {   // positive normal values only (enough for the test)
    uint32_t b; std::memcpy(&b, &f, 4);
    if (f == 0) return 0;
    return uint16_t(((b >> 23) - 112) << 10 | (b >> 13 & 0x3FF));
}
} // namespace

int main() {
    const uint32_t W = 640, H = 360;
    const auto a = fromY(ramp(W, H, 0), W, H), b = fromY(ramp(W, H, 8), W, H), c = fromY(ramp(W, H, 16), W, H);
    check(a.valid() && b.valid(), "luma grids from NV12");
    check(psnr(a, a) == 100.0, "identical pictures: 100 dB");
    const double moved = psnr(b, a);
    check(moved > 5 && moved < 40, "a moved picture scores lower (" + std::to_string(moved) + " dB)");
    // Limited range maps 16..235 onto 0..1.
    std::vector<uint8_t> grey(size_t(W) * H, 126);
    const auto g = fromY(grey, W, H, true);
    check(std::abs(g.y[0] - (126 - 16) / 219.0f) < 1e-4, "limited range expanded");
    // The fit maps a tone change (gain 0.8, offset 0.05) back exactly.
    Luma toned = a; for (auto& v : toned.y) v = 0.8f * v + 0.05f;
    const auto fit = fitLinear({&a}, {&toned});
    check(fit.valid && std::abs(fit.gain - 0.8) < 1e-3 && std::abs(fit.offset - 0.05) < 1e-3, "tone fit");
    check(psnr(apply(fit, a), toned) > 60, "fitted source matches the toned picture");
    check(!fitLinear({&a}, {}).valid, "no pairs: no fit");
    // A perfect generator beats blending; blending the neighbours of a moving ramp is not perfect.
    const Sample perfect = measure(b, b, a, c, Fit{1, 0, true}, {});
    check(perfect.psnr == 100.0 && perfect.blendPsnr < 60 && perfect.badShare == 0, "perfect generation beats the blend");
    const Sample repeat = measure(a, b, a, c, Fit{1, 0, true}, {});
    check(repeat.psnr < perfect.psnr && repeat.meanError > 0 && repeat.badShare > 0, "repeating the previous frame is scored as wrong");
    // HUD boxes: an error only inside the box shows there, not much in the whole picture.
    Luma hudBroken = b;
    for (uint32_t gy = 0; gy < 18; ++gy) for (uint32_t gx = 0; gx < 32; ++gx) hudBroken.y[size_t(gy) * kGridW + gx] = 1.0f;
    const Sample hud = measure(hudBroken, b, a, c, Fit{1, 0, true}, {Box{0, 0, 0.1f, 0.1f}});
    check(hud.hud && hud.hudPsnr < hud.psnr, "HUD artefacts are measured inside the box");
    // Output formats: 8-bit RGBA grey and linear FP16 grey give the same luma.
    std::vector<uint8_t> rgba(size_t(W) * H * 4);
    for (size_t i = 0; i < rgba.size(); i += 4) { rgba[i] = rgba[i + 1] = rgba[i + 2] = 128; rgba[i + 3] = 255; }
    Luma o8, o16;
    check(lumaFromOutput(rgba.data(), W * 4, W, H, OutputLayout::Rgba8, o8) && std::abs(o8.y[0] - 128 / 255.0f) < 1e-4, "RGBA8 output luma");
    const float linear = std::pow((128 / 255.0f + 0.055f) / 1.055f, 2.4f);
    std::vector<uint16_t> f16(size_t(W) * H * 4, toHalf(1.0f));
    for (size_t i = 0; i < f16.size(); i += 4) f16[i] = f16[i + 1] = f16[i + 2] = toHalf(linear);
    check(lumaFromOutput(reinterpret_cast<const uint8_t*>(f16.data()), W * 8, W, H, OutputLayout::Rgba16fLinear, o16) &&
          std::abs(o16.y[0] - o8.y[0]) < 0.004f, "FP16 linear output is sRGB-encoded before comparing");
    // Timing and the report.
    check(onTime(166667, 170000) && !onTime(166667, 260000) && !onTime(0, 1), "on-time rule");
    const auto report = summarize({perfect, repeat}, 38.0, 4, 100, 95);
    check(report.samples == 2 && report.onTimeShare() == 0.95 && report.psnr > repeat.psnr && report.psnr < 100, "report averages as MSE");
    const auto good = summarize({perfect}, 40, 1, 10, 10), bad = summarize({repeat}, 40, 1, 10, 5);
    check(good.score() > bad.score() && good.score() <= 100 && bad.score() >= 0, "score ranks the better method higher");
    check(summarize({}, 0, 0, 0, 0).score() == 0, "no samples: score 0");
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
