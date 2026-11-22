#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS fine-tuning of AI picture enhancement: shaping the change the enhancement makes.
//
// Veyra composes the enhancement as a change on top of the picture (shaders/NrResidualComposite.hlsl:
// result = picture + change) and already lets each enhancement layer scale that change as a whole
// and by direction (ResidualSettings: total, darken, brighten, color, luminance). This adds what
// those cannot do, for every layer at once:
//   shadows / midtones / highlights   how much of the change is kept in the dark, middle and bright
//                                     parts of the picture (0..2, 1 = as the model made it)
//   hue                               how far the change may move a colour's hue (0 keeps the
//                                     picture's own hues, 1 = as the model made it, up to 2)
//   maxBrighten / maxDarken           a soft limit on how far a pixel may get brighter or darker,
//                                     as a share of the range from black to reference white
//                                     (0 = no limit)
// The idea of shaping the enhancement's change this way is common to the community tools around
// this model (the request that led here showed a "result shaping" panel); the maths below and its
// shader are PushFPS's own.
//
// The values are live (read at every frame, no graph rebuild) and kept outside EnhancementSettings,
// like NIS sharpness, so the preset schema shared with Veyra is unchanged. They reach the shader
// packed into the four spare constants of its correction block, as whole percentages, with a flag:
// all four zero is "no shaping", which is what every other user of that shader passes, so nothing
// changes for them. Neutral values are sent as zeros too, and the shader then skips the code.
//
// The functions below are the reference the shader is tested against on WARP
// (tests/integration/PushGpuTests.cpp, testEnhanceShape).
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace veyra::pipeline {

struct PushEnhanceShape {
    // Whole percentages: 100 = unchanged for the first four, 0 = no limit for the last two.
    int shadows = 100, midtones = 100, highlights = 100, hue = 100;
    int maxBrighten = 0, maxDarken = 0;
    bool operator==(const PushEnhanceShape&) const = default;
    bool neutral() const { return shadows == 100 && midtones == 100 && highlights == 100 && hue == 100 && maxBrighten == 0 && maxDarken == 0; }
    PushEnhanceShape clamped() const {
        PushEnhanceShape s;
        s.shadows = std::clamp(shadows, 0, 200); s.midtones = std::clamp(midtones, 0, 200); s.highlights = std::clamp(highlights, 0, 200);
        s.hue = std::clamp(hue, 0, 200); s.maxBrighten = std::clamp(maxBrighten, 0, 100); s.maxDarken = std::clamp(maxDarken, 0, 100);
        return s;
    }
    // x: shadows + 256 midtones + 65536 highlights; y: hue + 256 maxBrighten + 65536 maxDarken;
    // z: unused; w: 1 = shape. Integers below 2^24, exact in a float. Neutral: all zero.
    std::array<float, 4> constants() const {
        const PushEnhanceShape s = clamped();
        if (s.neutral()) return {0.0f, 0.0f, 0.0f, 0.0f};
        return {float(s.shadows + 256 * s.midtones + 65536 * s.highlights), float(s.hue + 256 * s.maxBrighten + 65536 * s.maxDarken), 0.0f, 1.0f};
    }
};

// Process-wide, set by the interface (preferences pushShape*), read by the graph at every frame.
namespace push_enhance_shape_detail {
inline std::atomic<uint64_t>& store() {
    static std::atomic<uint64_t> value{0};   // 0 = neutral
    return value;
}
inline uint64_t pack(const PushEnhanceShape& in) {
    const PushEnhanceShape s = in.clamped();
    if (s.neutral()) return 0;
    return uint64_t(s.shadows) | uint64_t(s.midtones) << 8 | uint64_t(s.highlights) << 16 | uint64_t(s.hue) << 24 |
           uint64_t(s.maxBrighten) << 32 | uint64_t(s.maxDarken) << 40 | uint64_t(1) << 63;
}
inline PushEnhanceShape unpack(uint64_t v) {
    PushEnhanceShape s;
    if (!(v >> 63)) return s;
    s.shadows = int(v & 255); s.midtones = int((v >> 8) & 255); s.highlights = int((v >> 16) & 255); s.hue = int((v >> 24) & 255);
    s.maxBrighten = int((v >> 32) & 255); s.maxDarken = int((v >> 40) & 255);
    return s.clamped();
}
}
inline void setPushEnhanceShape(const PushEnhanceShape& shape) { push_enhance_shape_detail::store().store(push_enhance_shape_detail::pack(shape)); }
inline PushEnhanceShape pushEnhanceShape() { return push_enhance_shape_detail::unpack(push_enhance_shape_detail::store().load()); }

// --- reference maths (the shader does the same, in the same order) ----------------------------
namespace push_shape_ref {
struct Rgb { float r = 0, g = 0, b = 0; };
inline float luma(Rgb c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
// The working picture is linear light: 1.0 is white for SDR, and 80 nits for HDR, where the
// reference white for these controls is 203 nits. A gamma of 2.2 is close enough to how the eye
// spaces brightness for choosing a tonal range and for stating a limit.
inline float perceptual(float y, bool hdr) { return std::pow(std::max(y, 0.0f) / (hdr ? 203.0f / 80.0f : 1.0f), 1.0f / 2.2f); }
inline float linear(float p, bool hdr) { return std::pow(std::max(p, 0.0f), 2.2f) * (hdr ? 203.0f / 80.0f : 1.0f); }
inline float smoothstep(float a, float b, float x) { const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

// The layer's colour and brightness strengths (Veyra's `color` and `luminance`, 0..2 with 1 neutral),
// as the composite shader applies them before anything of PushFPS's. Up to 100 % this is the plain
// statement: the change's brightness part times `luminance`, its colour part times `color`.
//
// PushFPS 4.2.1: above 100 % the colour part tapers. A colour change twice as large as the one the
// enhancement made is fine where that change is small, and leaves the picture's own colours where
// it is large (a user's report: the slider "gets easily distorted"). The size of the change is its
// largest channel, measured against the pixel's own brightness (never under 2 % of white, so a
// black pixel's noise is not a large change); the factor is 1 + (color - 1) * knee / (knee + size).
// So a small change gets the full strength asked for, and what is added to any pixel's colour
// never passes (color - 1) * knee of its brightness: a quarter at 200 %.
inline constexpr float kPushColorKnee = 0.25f;
inline constexpr float kPushColorFloor = 0.02f;
inline float colorGain(float color, float baseLuma, float chromaPeak, bool hdr) {
    if (!(color > 1.0f)) return color;
    const float level = std::max(baseLuma, kPushColorFloor * (hdr ? 203.0f / 80.0f : 1.0f));
    return 1.0f + (color - 1.0f) * kPushColorKnee / (kPushColorKnee + chromaPeak / level);
}
inline Rgb colorDelta(Rgb base, Rgb delta, float color, float luminance, bool hdr) {
    if (color == 1.0f && luminance == 1.0f) return delta;
    const float dy = luma(delta);
    const Rgb chroma{delta.r - dy, delta.g - dy, delta.b - dy};
    const float peak = std::max(std::abs(chroma.r), std::max(std::abs(chroma.g), std::abs(chroma.b)));
    const float gain = colorGain(color, luma(base), peak, hdr);
    return {dy * luminance + chroma.r * gain, dy * luminance + chroma.g * gain, dy * luminance + chroma.b * gain};
}
// Before the layer's own strength: the change scaled by the picture's tonal range, and the part
// of its colour change that turns the hue scaled by `hue`.
inline Rgb shapeDelta(Rgb base, Rgb delta, const PushEnhanceShape& in, bool hdr) {
    const PushEnhanceShape s = in.clamped();
    const float yb = luma(base);
    const float t = std::clamp(perceptual(yb, hdr), 0.0f, 1.0f);
    const float gain = s.shadows / 100.0f * (1 - t) * (1 - t) + s.midtones / 100.0f * 2 * t * (1 - t) + s.highlights / 100.0f * t * t;
    delta = {delta.r * gain, delta.g * gain, delta.b * gain};
    if (s.hue != 100) {
        // The colour part of the change, split along the picture's own colour direction (more or
        // less saturated, same hue) and across it (a turn of the hue). Greys have no hue to keep.
        const float dy = luma(delta);
        Rgb dc{delta.r - dy, delta.g - dy, delta.b - dy};
        const Rgb cb{base.r - yb, base.g - yb, base.b - yb};
        const float length = std::sqrt(cb.r * cb.r + cb.g * cb.g + cb.b * cb.b);
        const float weight = smoothstep(0.02f, 0.10f, length / std::max(std::abs(yb), 1e-4f));
        if (length > 1e-6f && weight > 0.0f) {
            const Rgb dir{cb.r / length, cb.g / length, cb.b / length};
            const float along = dc.r * dir.r + dc.g * dir.g + dc.b * dir.b;
            const float across = 1.0f + (s.hue / 100.0f - 1.0f) * weight;
            dc = {along * dir.r + (dc.r - along * dir.r) * across, along * dir.g + (dc.g - along * dir.g) * across,
                  along * dir.b + (dc.b - along * dir.b) * across};
            delta = {dy + dc.r, dy + dc.g, dy + dc.b};
        }
    }
    return delta;
}
// After the layer's own strength: the soft limits on brightening and darkening. The change in the
// pixel's brightness is limited to limit * tanh(change / limit), on the perceptual scale, and the
// whole change is scaled to match. tanh is written out from exp with a clamped argument, and the
// scale is the first-order one where brightness hardly changes: see the shader for why.
inline float softKeep(float x) {
    return x < 0.1f ? 1.0f - x * x / 3.0f + 2.0f * x * x * x * x / 15.0f : (1.0f - 2.0f / (std::exp(2.0f * std::min(x, 10.0f)) + 1.0f)) / std::max(x, 0.1f);
}
inline Rgb capDelta(Rgb base, Rgb delta, const PushEnhanceShape& in, bool hdr) {
    const PushEnhanceShape s = in.clamped();
    const float yb = luma(base), dy = luma(delta);
    const float cap = (dy > 0 ? s.maxBrighten : s.maxDarken) / 100.0f;
    if (cap <= 0 || std::abs(dy) <= 1e-7f) return delta;
    const float pb = perceptual(yb, hdr), change = std::abs(perceptual(yb + dy, hdr) - pb);
    float keep = softKeep(change / cap);
    const float blend = smoothstep(0.01f, 0.03f, change);
    if (blend > 0) {
        const float limited = (dy > 0 ? 1.0f : -1.0f) * change * keep;
        const float exact = std::clamp((linear(pb + limited, hdr) - yb) / dy, 0.0f, 1.0f);
        keep = keep + (exact - keep) * blend;
    }
    return {delta.r * keep, delta.g * keep, delta.b * keep};
}
}  // namespace push_shape_ref
}  // namespace veyra::pipeline
