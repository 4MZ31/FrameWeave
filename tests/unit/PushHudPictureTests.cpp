// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS automatic HUD detection on whole pictures drawn the way games draw them: text straight
// over a moving scene, thin flat bars, text on a half-transparent panel, a minimap whose map scrolls,
// a counter whose digits change, in 1080p, 4K and a noisy 720p stream, plus what must not be found
// (letterbox and pillarbox bars, a flat or smooth sky, a scene without HUD). Each picture goes through
// reduce() and detect() as in the product. The first detector (3.1.7 and earlier) found no candidate
// cell in any of the HUD cases. Called from PushHudTests.cpp.
#include "veyra/engine/PushHud.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
using namespace veyra::engine::hud;
namespace {
int failures = 0;
void check(bool ok, const std::string& label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str()); if (!ok) ++failures; }
struct R { int x0, y0, x1, y1; };
// A moving game-like scene: large shapes, gradients and fine texture, panning.
static uint8_t sceneAt(int x, int y, int f, int pan) {
    const int sx = x + f * pan, sy = y + f * pan / 3;
    uint32_t h = uint32_t(sx / 3) * 374761393u + uint32_t(sy / 3) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    const double smooth = 120 + 55 * std::sin(sx * 0.011) + 35 * std::cos(sy * 0.017) + 25 * std::sin((sx + sy) * 0.05);
    return uint8_t(std::clamp(int(smooth) + int(h >> 28) * 3 - 22, 0, 255));
}
// Glyph-like strokes: 5x7 blocks of "letters" made of 3 px strokes, on a 0/1 mask.
static bool glyph(int x, int y, int scale) {
    const int cx = x / (8 * scale), cy = y / (12 * scale), lx = (x / scale) % 8, ly = (y / scale) % 12;
    if (lx >= 6 || ly >= 9) return false;                 // spacing between letters and lines
    uint32_t h = uint32_t(cx) * 2654435761u ^ uint32_t(cy) * 40503u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    const bool vert = (lx == 0 && (h & 1)) || (lx == 5 && (h & 2)) || (lx == 2 && (h & 4));
    const bool horiz = (ly == 0 && (h & 8)) || (ly == 4 && (h & 16)) || (ly == 8 && (h & 32));
    return vert || horiz || (lx == ly % 6 && (h & 64));
}
struct Hud {
    int skyRows = 0;            // top rows: a smooth, slowly changing sky (low texture, not HUD)
    std::vector<R> minimaps;    // round map: the content moves with the scene, ring and arrow stay
    std::vector<R> digits;      // a counter whose digits change between frames
    std::vector<R> text;        // bare text, drawn straight over the scene
    std::vector<R> bars;        // flat opaque bars
    std::vector<R> panels;      // half-transparent dark panel with text on it
    std::vector<R> flat;        // flat static areas that are not HUD (letterbox, sky)
    int noise = 0;
};
static std::vector<LumaFrame> frames(uint32_t w, uint32_t h, size_t count, int pan, const Hud& hud, int scale) {
    std::vector<LumaFrame> out(count);
    std::vector<uint8_t> px(size_t(w) * h * 4);
    uint32_t rng = 777;
    for (size_t f = 0; f < count; ++f) {
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                int v = sceneAt(int(x), int(y), int(f), pan);
                if (int(y) < hud.skyRows) v = int(150 + 30 * std::sin((int(x) + int(f) * pan) * 0.002) + 20.0 * int(y) / hud.skyRows);
                for (auto& r : hud.minimaps) {
                    const double cx = (r.x0 + r.x1) / 2.0, cy = (r.y0 + r.y1) / 2.0, rad = (r.x1 - r.x0) / 2.0;
                    const double d = std::hypot(double(x) - cx, double(y) - cy);
                    if (d <= rad) {
                        v = sceneAt(int(x) * 2, int(y) * 2, int(f), pan * 2) / 2 + 40;      // the map scrolls
                        if (d >= rad - 5 * scale / 3.0) v = 225;                             // ring
                        if (std::fabs(double(x) - cx) + std::fabs(double(y) - cy) < 4 * scale) v = 250;  // player arrow
                    }
                }
                for (auto& r : hud.digits) if (int(x) >= r.x0 && int(x) < r.x1 && int(y) >= r.y0 && int(y) < r.y1) {
                    // Left half fixed label, right half digits that change every other frame.
                    const bool right = int(x) >= (r.x0 + r.x1) / 2;
                    if (glyph(int(x) - r.x0 + (right ? int(f / 2) * 8 * scale * 3 : 0), int(y) - r.y0, scale)) v = 240;
                }
                for (auto& r : hud.flat) if (int(x) >= r.x0 && int(x) < r.x1 && int(y) >= r.y0 && int(y) < r.y1) v = 12;
                for (auto& r : hud.panels) if (int(x) >= r.x0 && int(x) < r.x1 && int(y) >= r.y0 && int(y) < r.y1) {
                    v = v / 2 + 10;
                    if (glyph(int(x) - r.x0, int(y) - r.y0, scale)) v = 235;
                }
                for (auto& r : hud.text) if (int(x) >= r.x0 && int(x) < r.x1 && int(y) >= r.y0 && int(y) < r.y1)
                    if (glyph(int(x) - r.x0, int(y) - r.y0, scale)) v = 240;
                for (auto& r : hud.bars) if (int(x) >= r.x0 && int(x) < r.x1 && int(y) >= r.y0 && int(y) < r.y1) v = 200;
                if (hud.noise) { rng = rng * 1664525u + 1013904223u; v += int((rng >> 24) % uint32_t(2 * hud.noise + 1)) - hud.noise; }
                uint8_t* p = px.data() + (size_t(y) * w + x) * 4;
                p[0] = p[1] = p[2] = uint8_t(std::clamp(v, 0, 255)); p[3] = 255;
            }
        if (!reduce(px.data(), size_t(w) * 4, w, h, PixelLayout::Rgba8, out[f])) { std::printf("reduce failed\n"); out.clear(); return out; }
    }
    return out;
}
static bool hits(const Detection& d, const R& t, uint32_t w, uint32_t h, float minCover = 0.8f, float maxGrow = 3.0f) {
    const float l = float(t.x0) / w, r = float(t.x1) / w, top = float(t.y0) / h, bot = float(t.y1) / h;
    for (auto& g : d.regions) {
        const float ix = std::max(0.0f, std::min(g.right, r) - std::max(g.left, l)), iy = std::max(0.0f, std::min(g.bottom, bot) - std::max(g.top, top));
        const float cover = ix * iy / ((r - l) * (bot - top));
        const float grow = (g.right - g.left) * (g.bottom - g.top) / ((r - l) * (bot - top));
        if (cover >= minCover && grow <= maxGrow) return true;
    }
    return false;
}
static void show(const char* name, const Detection& d) {
    std::printf("INFO %s: motion=%.2f candidates=%u regions=%zu tooStill=%d", name, d.motion, d.candidateCells, d.regions.size(), d.tooStill ? 1 : 0);
    for (auto& r : d.regions) std::printf(" [%.3f %.3f %.3f %.3f]", r.left, r.top, r.right, r.bottom);
    std::printf("\n");
}
} // namespace

int runHudPictureTests() {
    failures = 0;
    const uint32_t W = 1920, H = 1080;
    {   // Bare text over the moving scene (money and ammo top right), no panel behind it.
        Hud hud; const R text{1560, 60, 1860, 130}; hud.text = {text};
        const auto d = detect(frames(W, H, 12, 23, hud, 3)); show("bare text", d);
        check(hits(d, text, W, H), "bare text over the moving scene is found");
    }
    {   // A thin flat health bar (10 px tall) and a second one under it.
        Hud hud; const R bar{60, 1000, 400, 1010}, bar2{60, 1020, 300, 1030}; hud.bars = {bar, bar2};
        const auto d = detect(frames(W, H, 12, 23, hud, 3)); show("thin bars", d);
        check(hits(d, R{60, 1000, 400, 1030}, W, H, 0.7f, 4.0f), "thin flat bars are found");
    }
    {   // Text on a half-transparent panel: the panel changes with the scene behind it.
        Hud hud; const R panel{80, 80, 420, 200}; hud.panels = {panel};
        const auto d = detect(frames(W, H, 12, 23, hud, 3)); show("panel", d);
        check(hits(d, panel, W, H, 0.6f), "text on a half-transparent panel is found");
    }
    {   // The same HUD in a 4K picture with capture noise.
        Hud hud; const R text{3100, 120, 3700, 260}; hud.text = {text}; hud.noise = 2;
        const auto d = detect(frames(3840, 2160, 12, 40, hud, 6)); show("4K noisy text", d);
        check(hits(d, text, 3840, 2160), "bare text in 4K with capture noise is found");
    }
    {   // 720p (Remote Play) with stronger noise, as a compressed stream has.
        Hud hud; const R text{980, 40, 1220, 100}; hud.text = {text}; hud.noise = 4;
        const auto d = detect(frames(1280, 720, 14, 15, hud, 2)); show("720p noisy text", d);
        check(hits(d, text, 1280, 720), "bare text in a noisy 720p stream is found");
    }
    {   // Not HUD: letterbox bars, a flat static sky and nothing else.
        Hud hud; hud.flat = {{0, 0, 1920, 130}, {0, 950, 1920, 1080}};
        const auto d = detect(frames(W, H, 12, 23, hud, 3)); show("letterbox", d);
        check(d.regions.empty(), "letterbox bars give no regions");
        Hud sky; sky.flat = {{0, 0, 1920, 420}}; sky.noise = 1;
        const auto e = detect(frames(W, H, 12, 23, sky, 3)); show("flat sky", e);
        check(e.regions.empty(), "a flat static sky gives no regions");
        Hud pillar; pillar.flat = {{0, 0, 240, 1080}, {1680, 0, 1920, 1080}};
        const auto g = detect(frames(W, H, 12, 23, pillar, 3)); show("pillarbox", g);
        check(g.regions.empty(), "pillarbox bars give no regions");
    }
    {   // A moving scene with no HUD at all, with noise.
        Hud hud; hud.noise = 2;
        const auto d = detect(frames(W, H, 12, 23, hud, 3)); show("no hud", d);
        check(d.regions.empty(), "a moving scene without HUD gives no regions");
    }
    {   // Slow camera: little motion, HUD still told apart.
        Hud hud; const R text{1560, 60, 1860, 130}; hud.text = {text}; hud.noise = 1;
        const auto d = detect(frames(W, H, 14, 5, hud, 3)); show("slow pan", d);
        check(d.tooStill || hits(d, text, W, H), "a slow pan finds the text or asks for more motion");
    }
    {   // Everything at once: minimap panel, bars, text.
        Hud hud; const R text{1560, 60, 1860, 130}, panel{60, 760, 360, 980}, bar{60, 1000, 400, 1012};
        hud.text = {text}; hud.panels = {panel}; hud.bars = {bar}; hud.noise = 2;
        const auto d = detect(frames(W, H, 12, 23, hud, 3)); show("full hud", d);
        check(hits(d, text, W, H) , "full HUD: text found");
        check(hits(d, R{60, 760, 400, 1012}, W, H, 0.6f) || (hits(d, panel, W, H, 0.6f)), "full HUD: minimap panel found");
        check(d.regions.size() <= 4, "at most four regions");
    }
    {   // A smooth slowly changing sky over a moving scene is not HUD.
        Hud hud; hud.skyRows = 420; hud.noise = 1;
        const auto d = detect(frames(W, H, 12, 23, hud, 3)); show("smooth sky", d);
        check(d.regions.empty(), "a smooth sky gives no regions");
        const R text{1560, 60, 1860, 130}; hud.text = {text};
        const auto e = detect(frames(W, H, 12, 23, hud, 3)); show("text over smooth sky", e);
        check(hits(e, text, W, H), "text over a smooth sky is found");
    }
    {   // A round minimap whose map scrolls: the ring and the arrow hold, the box covers the map.
        Hud hud; const R map{60, 780, 320, 1040}; hud.minimaps = {map}; hud.noise = 1;
        const auto d = detect(frames(W, H, 12, 23, hud, 3)); show("minimap", d);
        check(hits(d, map, W, H, 0.85f, 1.6f), "a minimap with a scrolling map is found by its ring");
    }
    {   // A counter: the label holds, the digits change.
        Hud hud; const R counter{1500, 980, 1860, 1040}; hud.digits = {counter};
        const auto d = detect(frames(W, H, 12, 23, hud, 3)); show("counter", d);
        check(hits(d, counter, W, H, 0.45f), "a counter with changing digits is found at least by its label");
    }
    {   // A small picture (640 x 360, as the GPU test and low-resolution streams give) and only six
        // frames: the HUD box and nothing else. With a softer stillness bound for thin shapes, points
        // of the moving scene that varied little over six frames gave three more boxes.
        const uint32_t w = 640, h = 360;
        std::vector<LumaFrame> six(6);
        std::vector<uint8_t> px(size_t(w) * h * 4);
        bool reduced = true;
        for (int step = 0; step < 6; ++step) {
            const int shift = step * 21;
            for (uint32_t y = 0; y < h; ++y)
                for (uint32_t x = 0; x < w; ++x) {
                    uint8_t v;
                    if (x >= 40 && x < 200 && y >= 20 && y < 60) v = ((x / 4 + y / 5) % 3 == 0 || x % 9 == 0) ? 235 : 20;
                    else {
                        const int sx = int(x) + shift, sy = int(y) + shift / 2;
                        uint32_t hh = uint32_t(sx / 2) * 374761393u + uint32_t(sy / 2) * 668265263u;
                        hh = (hh ^ (hh >> 13)) * 1274126177u;
                        v = uint8_t(std::clamp(int(128 + 60 * std::sin(sx * 0.03) + 40 * std::cos(sy * 0.04)) + int(hh >> 28) * 4 - 30, 0, 255));
                    }
                    uint8_t* p = px.data() + (size_t(y) * w + x) * 4;
                    p[0] = p[1] = p[2] = v; p[3] = 255;
                }
            reduced = reduced && reduce(px.data(), size_t(w) * 4, w, h, PixelLayout::Rgba8, six[size_t(step)]);
        }
        const auto d = reduced ? detect(six) : Detection{}; show("640x360, six frames", d);
        check(d.regions.size() == 1 && hits(d, R{40, 20, 200, 60}, w, h, 0.95f, 1.8f), "a small picture and six frames give the HUD box and nothing else");
    }
    return failures;
}
