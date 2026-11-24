// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Crosshair Hold (pipeline/PushOverlay.h): a crosshair that stands still over a moving picture is taken out
// of what frame generation is given and laid back over what it makes; nothing that belongs to the moving
// picture is. Here the rule itself, on drawn pictures with a known motion, so the true in-between frame is
// known. The reticle is the one measured on real frames (three strokes four pixels thick with a dark outline,
// about half see-through). The shaders are compared with these same functions on a graphics card in
// tests/integration/PushGpuTests.cpp. Pure: builds anywhere.
#include "veyra/pipeline/PushOverlay.h"
#include <cmath>
#include <cstdio>
#include <string>
using namespace veyra::pipeline::overlay;
static int failures = 0;
static void check(bool ok, const std::string& label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str()); if (!ok) ++failures; }

// A world to pan over: several layers of smooth noise, with detail down to a few pixels.
struct World {
    int w, h; std::vector<float> v;
    World(int w_, int h_, uint32_t seed, float contrast = 0.20f) : w(w_), h(h_), v(size_t(w_) * h_, 0.42f) {
        uint32_t s = seed;
        auto rnd = [&] { s = s * 1664525u + 1013904223u; return float((s >> 8) & 0xffff) / 65535.0f - 0.5f; };
        float amp = contrast;
        for (int cell : {96, 48, 24, 12, 6}) {
            const int gw = w / cell + 2, gh = h / cell + 2;
            std::vector<float> grid(size_t(gw) * gh);
            for (auto& g : grid) g = rnd();
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const float fx = float(x) / cell, fy = float(y) / cell; const int ix = int(fx), iy = int(fy);
                    const float tx = fx - ix, ty = fy - iy;
                    const float a = grid[size_t(iy) * gw + ix], b = grid[size_t(iy) * gw + ix + 1], c = grid[size_t(iy + 1) * gw + ix], d = grid[size_t(iy + 1) * gw + ix + 1];
                    v[size_t(y) * w + x] += amp * ((a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty) * 2.0f;
                }
            amp *= 0.6f;
        }
        for (auto& p : v) p = std::clamp(p, 0.02f, 0.98f);
    }
    float at(int x, int y) const { x = ((x % w) + w) % w; y = ((y % h) + h) % h; return v[size_t(y) * w + x]; }
    // the zone when the world has moved by (2 * hx, 2 * hy) half pixels: whole and half steps, so that the frame
    // half way between two frames is exact
    Picture view(int n, int hx, int hy) const {
        Picture p(n);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const int x0 = x - (hx >> 1), y0 = y - (hy >> 1);
                float value = at(x0, y0);
                if (hx & 1) value = 0.5f * (value + at(x0 - 1, y0));
                if (hy & 1) { float other = at(x0, y0 - 1); if (hx & 1) other = 0.5f * (other + at(x0 - 1, y0 - 1)); value = 0.5f * (value + other); }
                p.at(x, y) = {value, value * 0.96f, value * 0.90f};
            }
        return p;
    }
};
// The reticle: opacity and colour per pixel.
struct Reticle {
    int size; std::vector<float> alpha, colour;
    explicit Reticle(int n, float coreAlpha = 0.52f, int spread = 0) : size(n), alpha(size_t(n) * n, 0), colour(size_t(n) * n, 0) {
        const int c = n / 2;
        const int strokes[3][4] = {{c - 12, c - 8, c - 36 - spread, c - 12 - spread}, {c - 12, c - 8, c + 12 + spread, c + 36 + spread}, {c + spread / 2, c + 21 + spread / 2, c - 2, c + 2}};
        for (const auto& s : strokes)
            for (int y = s[0] - 1; y < s[1] + 1; ++y)
                for (int x = s[2] - 1; x < s[3] + 1; ++x) { alpha[size_t(y) * n + x] = 0.67f; colour[size_t(y) * n + x] = 0; }
        for (const auto& s : strokes)
            for (int y = s[0]; y < s[1]; ++y)
                for (int x = s[2]; x < s[3]; ++x) { alpha[size_t(y) * n + x] = coreAlpha; colour[size_t(y) * n + x] = 0.86f; }
    }
    bool covers(int x, int y) const { return alpha[size_t(y) * size + x] > 0; }
    Picture over(const Picture& bg) const {
        Picture p(bg.size);
        for (size_t i = 0; i < bg.v.size(); ++i) {
            const float a = alpha[i], k = colour[i]; const Colour& b = bg.v[i];
            p.v[i] = {b.r * (1 - a) + k * a, b.g * (1 - a) + k * a, b.b * (1 - a) + k * a};
        }
        return p;
    }
};
static int countOf(const Plane& m) { int n = 0; for (float v : m.v) n += v > 0 ? 1 : 0; return n; }
static float diff(const Colour& a, const Colour& b) { return std::max({std::fabs(a.r - b.r), std::fabs(a.g - b.g), std::fabs(a.b - b.b)}) * 255.0f; }
// frame generation as good as it can be: both frames moved half way with the true motion and averaged
static Picture between(const Picture& a, const Picture& b, int vx, int vy) {
    const int n = a.size; Picture g(n);
    auto half = [&](const Picture& p, int x, int y, int hx, int hy) {      // p moved by (hx, hy) half pixels
        auto px = [&](int qx, int qy) { qx = std::clamp(qx, 0, n - 1); qy = std::clamp(qy, 0, n - 1); return p.at(qx, qy); };
        const int x0 = x - (hx >> 1), y0 = y - (hy >> 1);
        Colour c = px(x0, y0);
        auto mix = [](Colour u, Colour w) { return Colour{0.5f * (u.r + w.r), 0.5f * (u.g + w.g), 0.5f * (u.b + w.b)}; };
        if (hx & 1) c = mix(c, px(x0 - 1, y0));
        if (hy & 1) { Colour o = px(x0, y0 - 1); if (hx & 1) o = mix(o, px(x0 - 1, y0 - 1)); c = mix(c, o); }
        return c;
    };
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const Colour p = half(a, x, y, vx, vy), q = half(b, x, y, -vx, -vy);
            g.at(x, y) = {0.5f * (p.r + q.r), 0.5f * (p.g + q.g), 0.5f * (p.b + q.b)};
        }
    return g;
}
struct Run {
    int marked = 0, markedElsewhere = 0, firstHeld = -1;
    float reticleToday = 0, reticleNew = 0, besideToday = 0, besideNew = 0, through = 0;
    int offToday = 0, offNew = 0;
};
// Pans the world by (vx, vy) pixels a frame under a reticle and judges the last pairs against the true frame.
static Run pan(const World& world, int vx, int vy, float coreAlpha, bool reticleOn, int frames = 14, int spreadFrom = -1) {
    const int n = 400; const Rule r = ruleFor(2160);
    State state(n); Run out;
    Picture prev, prevFilled; Plane prevMask(n);
    int judged = 0;
    for (int k = 0; k < frames; ++k) {
        const int spread = (spreadFrom >= 0 && k > spreadFrom) ? std::min(2 * (k - spreadFrom), 18) : 0;
        const Reticle ret(n, reticleOn ? coreAlpha : 0.0f, spread);
        Reticle none(n, 0.0f); std::fill(none.alpha.begin(), none.alpha.end(), 0.0f);
        const Reticle& shown = reticleOn ? ret : none;
        const Picture frame = shown.over(world.view(n, 2 * vx * k, 2 * vy * k));
        const Plane mask = step(state, lumaPlane(frame, 0), r);
        const Picture filled = fill(frame, mask, r);
        int onReticle = 0, elsewhere = 0;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (mask.at(x, y) > 0) {
                    bool nearReticle = false;
                    for (int j = -4; j <= 4 && !nearReticle; ++j)
                        for (int i = -4; i <= 4 && !nearReticle; ++i) nearReticle = shown.covers(std::clamp(x + i, 0, n - 1), std::clamp(y + j, 0, n - 1));
                    (nearReticle ? onReticle : elsewhere)++;
                }
        out.markedElsewhere = std::max(out.markedElsewhere, elsewhere);
        if (onReticle > 300 && out.firstHeld < 0) out.firstHeld = k;
        if (k >= frames - 3 && k > 0) {
            out.marked = onReticle;
            const Picture truth = shown.over(world.view(n, vx * (2 * k - 1), vy * (2 * k - 1)));
            const Picture today = between(prev, frame, vx, vy), clean = between(prevFilled, filled, vx, vy);
            float rt = 0, rn = 0, m = 0; int rc = 0;
            for (int y = 30; y < n - 30; ++y)
                for (int x = 30; x < n - 30; ++x) {
                    const Colour made = composeAt(prev, frame, prevFilled, filled, clean, prevMask, mask, x, y, 0.5f);
                    const float et = diff(today.at(x, y), truth.at(x, y)), en = diff(made, truth.at(x, y));
                    if (shown.covers(x, y)) { rt += et; rn += en; ++rc; m += showsThrough(prev, frame, prevFilled, filled, prevMask, mask, x, y); }
                    else { out.besideToday = std::max(out.besideToday, et); out.besideNew = std::max(out.besideNew, en); out.offToday += et > 8 ? 1 : 0; out.offNew += en > 8 ? 1 : 0; }
                }
            if (rc) { out.reticleToday += rt / rc; out.reticleNew += rn / rc; out.through += m / rc; }
            ++judged;
        }
        prev = frame; prevFilled = filled; prevMask = mask;
    }
    if (judged) { out.reticleToday /= judged; out.reticleNew /= judged; out.through /= judged; out.offToday /= judged; out.offNew /= judged; }
    return out;
}

int main() {
    {   // sizes
        const Rule a = ruleFor(2160), b = ruleFor(1080), c = ruleFor(720);
        check(a.flank == 5 && a.longest == 110 && a.shortest == 4 && a.fresh == 24 && a.grow == 3 && a.reach == 24 && a.hole == 9 && a.stand == 4 && a.memory == 60, "the rule's numbers at 2160 lines");
        check(b.flank == 3 && b.longest == 55 && b.shortest == 2 && b.fresh == 12 && b.grow == 2 && b.reach == 12 && b.hole == 5 && b.stand == 4, "half of them at 1080 lines; the frames to stand are the same");
        check(c.flank == 2 && c.grow == 1 && c.reach == 8, "and a floor under them at 720");
        const Zone z = zoneFor(3840, 2160), s = zoneFor(1920, 1080), tiny = zoneFor(160, 90);
        check(z.size == 400 && z.x == 1720 && z.y == 880, "the zone at 4K: 400 pixels round the centre");
        check(s.size == 192 && s.x % 2 == 0 && s.y % 2 == 0 && s.size % kBlock == 0, "at 1080 lines a whole number of blocks on even pixels");
        check(tiny.size == 0, "a picture too small for a zone gets none");
    }
    {   // the count
        const Rule r = ruleFor(2160);
        float c = 0;
        c = nextCount(c, true, false, r); check(c == 1 && isBar(c) && !hasStood(c, r), "a bar first seen in a still frame: a bar, not yet counted");
        for (int i = 0; i < 3; ++i) c = nextCount(c, true, true, r);
        check(c == 4 && !hasStood(c, r), "three moving frames are not enough");
        c = nextCount(c, true, true, r); check(c == 5 && hasStood(c, r), "four are");
        c = nextCount(c, true, false, r); check(c == 5, "a still frame neither adds nor takes away");
        c = nextCount(c, false, false, r); check(c == -1 && !isBar(c) && !longNoBar(c, r), "the frame it is no bar, everything is lost");
        for (int i = 0; i < 10; ++i) c = nextCount(c, false, true, r);
        check(c == -5 && longNoBar(c, r), "no bar for four moving frames: long no bar, and it goes no lower");
        float d = nextCount(0, true, true, r); check(d == 2, "a bar first seen in a moving frame has one counted frame");
        float e = nextCount(0, false, true, r); check(e == -2, "and likewise for no bar");
    }
    const World busy(1400, 700, 7u), other(1400, 700, 99u, 0.28f), dim(1400, 700, 5u, 0.06f);
    {   // the reticle over a moving picture: found, filled, laid back
        struct Case { const World* w; int vx, vy; const char* name; };
        const Case cases[] = {{&busy, 6, 0, "6 px level"}, {&busy, 20, 0, "20 px level"}, {&busy, 60, 0, "60 px level"}, {&busy, 24, 8, "24 and 8 px"},
                              {&other, 0, 14, "14 px upright"}, {&other, 33, 5, "33 and 5 px"}};
        for (const auto& c : cases) {
            const Run half = pan(*c.w, c.vx, c.vy, 0.52f, true), solid = pan(*c.w, c.vx, c.vy, 1.0f, true);
            std::printf("  %-14s half see-through: held from frame %d, %d px marked (%d elsewhere); reticle %.1f -> %.1f; beside worst %.1f -> %.1f, off by >8: %d -> %d; shows through %.2f\n",
                        c.name, half.firstHeld, half.marked, half.markedElsewhere, half.reticleToday, half.reticleNew, half.besideToday, half.besideNew, half.offToday, half.offNew, half.through);
            check(half.firstHeld >= 1 && half.firstHeld <= 5, std::string(c.name) + ": the reticle is held after four counted frames");
            check(half.markedElsewhere == 0, std::string(c.name) + ": nothing of the moving picture is marked");
            std::printf("  %-14s solid:            reticle %.1f -> %.1f; off by >8: %d -> %d; shows through %.2f\n", "", solid.reticleToday, solid.reticleNew, solid.offToday, solid.offNew, solid.through);
            check(half.reticleToday > 20 && half.reticleNew < 5.0f && half.reticleNew * 10 < half.reticleToday, std::string(c.name) + ": a half see-through reticle is as it should be in the generated frame");
            check(solid.reticleToday > 30 && solid.reticleNew < 5.0f && solid.reticleNew * 10 < solid.reticleToday, std::string(c.name) + ": and a solid one");
            check(half.offNew * 4 <= half.offToday && half.besideNew < half.besideToday, std::string(c.name) + ": the ghosts beside it are gone");
            // over all the reticle's pixels: the core (0.48 lets through), its outline (0.33) and what the 5 x 5 window takes in
            check(half.through > 0.30f && half.through < 0.80f, std::string(c.name) + ": the rule finds that about half shows through");
            check(solid.through + 0.10f < half.through, std::string(c.name) + ": and less through a solid one");
        }
    }
    {   // nothing is marked where there is no overlay
        int worst = 0;
        for (const World* w : {&busy, &other, &dim})
            for (const auto& v : {std::pair{3, 0}, {6, 0}, {0, 3}, {0, 6}, {10, 0}, {4, 4}, {20, 0}, {0, 20}, {33, 5}, {60, 0}, {1, 0}, {0, 1}}) {
                const Run run = pan(*w, v.first, v.second, 0.0f, false, 9);
                worst = std::max(worst, run.markedElsewhere);
            }
        std::printf("  a moving picture alone, 36 pans: most pixels ever marked %d\n", worst);
        check(worst == 0, "a moving picture without an overlay: no pixel is ever marked");
        const Run still = pan(busy, 0, 0, 0.52f, true, 12);
        check(still.marked == 0 && still.markedElsewhere == 0, "a picture that stands still: nothing is counted, nothing is marked");
    }
    {   // a line of the world sliding along itself is not an overlay
        const int n = 400; const Rule r = ruleFor(2160); State state(n); int marked = 0;
        for (int k = 0; k < 16; ++k) {
            Picture frame = busy.view(n, 2 * 9 * k, 0);
            for (int x = 0; x < n; ++x) {                       // a bright dash 70 px long, moving 9 px a frame with the world
                const int wx = x - 9 * k;
                if (((wx % 160) + 160) % 160 < 70) for (int y = 150; y < 154; ++y) frame.at(x, y) = {0.95f, 0.95f, 0.95f};
            }
            marked = std::max(marked, countOf(step(state, lumaPlane(frame, 0), r)));
        }
        check(marked == 0, "a dash of the world sliding along itself with the pan is never marked");
    }
    {   // what stands still while something passes elsewhere in the zone is scenery, not an overlay
        const int n = 400; const Rule r = ruleFor(2160); State far(n), beside(n); int farMarked = 0, besideMarked = 0, zoneMoved = 0;
        for (int k = 0; k < 14; ++k) {
            // The picture stands still, except a band across the lower part that pans 12 px a frame (traffic passing).
            Picture frame = busy.view(n, 0, 0);
            const Picture band = busy.view(n, 2 * 12 * k, 0);
            for (int y = 256; y < 352; ++y) for (int x = 0; x < n; ++x) frame.at(x, y) = band.at(x, y);
            Picture one = frame, two = frame;
            // a bright bar that stands still far above the band (a lamp post's arm), and one inside the band
            for (int y = 60; y < 64; ++y) for (int x = 150; x < 190; ++x) one.at(x, y) = {0.95f, 0.95f, 0.95f};
            for (int y = 300; y < 304; ++y) for (int x = 150; x < 190; ++x) two.at(x, y) = {0.95f, 0.95f, 0.95f};
            farMarked = std::max(farMarked, countOf(step(far, lumaPlane(one, 0), r)));
            besideMarked = std::max(besideMarked, countOf(step(beside, lumaPlane(two, 0), r)));
            zoneMoved += far.moving ? 1 : 0;
        }
        check(zoneMoved >= 12, "a band of the zone that moves makes the zone count as moving");
        check(farMarked == 0, "a bar that stands still far from what moves is never marked");
        check(besideMarked > 200, "a bar that stands still in the middle of what moves is held");
        // the neighbourhood itself
        const int blocks = 25; std::vector<unsigned char> flags(size_t(blocks) * blocks, 0);
        flags[size_t(10) * blocks + 10] = 1;
        check(movesNear(flags, blocks, 10 * 16 + 3, 10 * 16 + 3) && movesNear(flags, blocks, 9 * 16, 11 * 16 + 15) && movesNear(flags, blocks, 11 * 16 + 15, 9 * 16),
              "near: the moving block itself and the blocks round it");
        check(!movesNear(flags, blocks, 12 * 16, 10 * 16) && !movesNear(flags, blocks, 10 * 16, 8 * 16 + 15), "and no further");
        flags.assign(flags.size(), 0); flags[0] = 1; flags[flags.size() - 1] = 1;
        check(movesNear(flags, blocks, 0, 0) && movesNear(flags, blocks, 399, 399) && !movesNear(flags, blocks, 399, 0), "the zone's corners have fewer blocks round them");
    }
    {   // crosshairs of other shapes: a dot, a ring, a dot with an outline that is too wide to be a bar itself
        const int n = 400; const Rule r = ruleFor(2160); const float c = 199.5f;
        struct Shape { const char* name; float inner, outer; int edge; int least; bool insideStays; };
        // inner..outer: the bright part's radii (a dot has inner 0); edge: a dark outline this wide
        for (const Shape& shape : {Shape{"a dot 6 pixels across", 0, 3, 0, 100, false}, Shape{"a dot 8 pixels across with an outline", 0, 4, 2, 100, false},
                                   Shape{"a dot 12 pixels across with an outline", 0, 6, 2, 100, false}, Shape{"a ring 32 across, 2 thick", 15, 17, 0, 97, true},
                                   Shape{"a ring 48 across, 3 thick, with an outline", 22.5f, 25.5f, 1, 97, true}, Shape{"a ring 96 across, 4 thick", 46, 50, 0, 97, true}}) {
            std::vector<float> alpha(size_t(n) * n, 0), colour(size_t(n) * n, 0);
            int total = 0;
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x) {
                    const float q = std::hypot(float(x) - c, float(y) - c);
                    if (q >= shape.inner && q <= shape.outer) { alpha[size_t(y) * n + x] = 0.9f; colour[size_t(y) * n + x] = 0.9f; ++total; }
                    else if (shape.edge > 0 && q > shape.inner - float(shape.edge) && q < shape.outer + float(shape.edge)) { alpha[size_t(y) * n + x] = 0.67f; ++total; }
                }
            bool everyPan = true; int worstShare = 100, elsewhere = 0, first = -1, insideMarked = 0;
            for (const auto& v : {std::pair{20, 0}, {13, 7}, {0, 9}}) {
                State state(n); int covered = 0;
                for (int k = 0; k < 9; ++k) {
                    Picture p = busy.view(n, 2 * v.first * k, 2 * v.second * k);
                    for (size_t i = 0; i < p.v.size(); ++i) { const float a = alpha[i], q = colour[i]; Colour& b = p.v[i]; b = {b.r * (1 - a) + q * a, b.g * (1 - a) + q * a, b.b * (1 - a) + q * a}; }
                    const Plane m = step(state, lumaPlane(p, 0), r);
                    covered = 0; int far = 0; insideMarked = 0;
                    for (int y = 0; y < n; ++y)
                        for (int x = 0; x < n; ++x) {
                            if (!(m.at(x, y) > 0)) continue;
                            const float q = std::hypot(float(x) - c, float(y) - c);
                            if (alpha[size_t(y) * n + x] > 0) ++covered;
                            else if (q > shape.outer + float(shape.edge) + 5.0f) ++far;
                            else if (q < shape.inner - float(shape.edge) - 5.0f) ++insideMarked;
                        }
                    elsewhere = std::max(elsewhere, far);
                    if (covered > 0 && first < 0) first = k;
                }
                worstShare = std::min(worstShare, covered * 100 / total);
                everyPan = everyPan && covered * 100 / total >= shape.least;
            }
            std::printf("  %-44s %3d %% of its %4d px taken out, from frame %d; %d px elsewhere, %d inside it\n", shape.name, worstShare, total, first, elsewhere, insideMarked);
            check(everyPan && first == 4, std::string(shape.name) + ": held like the strokes");
            check(elsewhere == 0, std::string(shape.name) + ": nothing of the moving picture is marked");
            if (shape.insideStays) check(insideMarked == 0, std::string(shape.name) + ": the picture inside it is left to frame generation");
        }
        // the inside of a small closed shape, one pixel at a time
        Plane ring(64);
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) { const float q = std::hypot(float(x) - 31.5f, float(y) - 31.5f); if (q >= 7 && q <= 9) ring.at(x, y) = 1; }
        check(enclosed(ring, 31, 31, 9) && enclosed(ring, 33, 30, 9), "inside a ring 16 across: an accepted bar near on all four sides");
        check(!enclosed(ring, 34, 29, 9), "a pixel whose far side is further than the reach is left to the pixels grown round the bars");
        check(!enclosed(ring, 31, 31, 6), "but not when the ring is wider than the reach");
        check(!enclosed(ring, 45, 31, 9) && !enclosed(ring, 31, 10, 9), "and never outside it");
        Plane open(64); for (int y = 20; y < 44; ++y) { open.at(22, y) = 1; open.at(40, y) = 1; } for (int x = 22; x <= 40; ++x) open.at(x, 20) = 1;
        check(!enclosed(open, 31, 30, 9), "a shape open on one side encloses nothing");
    }
    {   // a reticle whose strokes move after it was found is followed
        const Run moving = pan(busy, 20, 0, 0.52f, true, 16, 8);
        std::printf("  strokes moving out 2 px a frame from frame 8: %d px marked, reticle %.1f -> %.1f\n", moving.marked, moving.reticleToday, moving.reticleNew);
        check(moving.marked > 600, "strokes that move in and out stay held once found");
        check(moving.reticleNew * 3 < moving.reticleToday, "and are far closer to the true frame than today");
    }
    {   // the fill and the weight, one pixel at a time
        const int n = 64; const Rule r = ruleFor(2160);
        Picture flat(n); for (auto& c : flat.v) c = {0.3f, 0.4f, 0.5f};
        Plane mask(n); for (int y = 30; y < 34; ++y) for (int x = 20; x < 44; ++x) { mask.at(x, y) = 1; flat.at(x, y) = {1, 1, 1}; }
        const Picture f = fill(flat, mask, r);
        bool gone = true; for (int y = 30; y < 34; ++y) for (int x = 20; x < 44; ++x) gone = gone && diff(f.at(x, y), {0.3f, 0.4f, 0.5f}) < 0.01f;
        check(gone, "a masked bar on a flat picture is filled with the picture's own colour");
        check(diff(f.at(5, 5), flat.at(5, 5)) == 0, "a pixel outside the mask is not touched");
        Plane all(n, 1.0f); const Picture stuck = fill(flat, all, r);
        check(diff(stuck.at(32, 32), flat.at(32, 32)) == 0, "a masked pixel with nothing unmasked in reach stays as it is");
        const Colour same = composeAt(flat, flat, f, f, f, Plane(n), Plane(n), 32, 32, 0.5f);
        check(diff(same, f.at(32, 32)) == 0, "outside both masks the generated frame is not touched");
    }
    {   // Subtitle Hold: the same rule in a rectangular band along the bottom.
        const Zone z = subtitleZoneFor(3840, 2160);
        check(z.w == 2400 && z.h == 400 && z.x == 720 && z.y == 1674 && z.size == z.w, "the subtitle band at 4K: the middle 2400 x 400, above the bottom edge");
        const Zone s = subtitleZoneFor(1920, 1080);
        check(s.w % kBlock == 0 && s.h % kBlock == 0 && s.x % 2 == 0 && s.y % 2 == 0 && s.y + s.h <= 1080, "at 1080 lines whole blocks on even pixels, inside the picture");
        check(subtitleZoneFor(320, 180).w == 0, "a picture too small gets no band");
        check(zoneFor(3840, 2160).w == 400 && zoneFor(3840, 2160).h == 400, "the crosshair's zone is still its square");
        // A line of text (white strokes with a dark outline) standing still while the road runs towards the
        // camera (the world moves down two pixels a frame): found and held; the road is not.
        const int W = 640, H = 128; const Rule r = ruleFor(2160);
        World road(1100, 1100, 9);
        std::vector<float> alpha(size_t(W) * H, 0), colour(size_t(W) * H, 0);
        auto stroke = [&](int x0, int y0, int x1, int y1) {
            for (int y = y0 - 2; y < y1 + 2; ++y) for (int x = x0 - 2; x < x1 + 2; ++x) { alpha[size_t(y) * W + x] = 0.8f; colour[size_t(y) * W + x] = 0.05f; }
        };
        auto core = [&](int x0, int y0, int x1, int y1) {
            for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) { alpha[size_t(y) * W + x] = 1.0f; colour[size_t(y) * W + x] = 0.97f; }
        };
        for (int letter = 0; letter < 14; ++letter) {   // letters of 3 px strokes, 26 px high, 30 px apart
            const int x = 40 + letter * 40, y = 50;
            const int shape[3][4] = {{x, y, x + 3, y + 26}, {x, y, x + 18, y + 3}, {x + 15, y + 12, x + 18, y + 26}};
            for (const auto& q : shape) stroke(q[0], q[1], q[2], q[3]);
            for (const auto& q : shape) core(q[0], q[1], q[2], q[3]);
        }
        State state(W, H); int textHeld = 0, roadMarked = 0, textPixels = 0;
        for (size_t i = 0; i < alpha.size(); ++i) textPixels += colour[i] > 0.5f ? 1 : 0;
        for (int f = 0; f < 14; ++f) {
            Picture p(W, H);
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
                const float g = road.at(x, y - 2 * f), a = alpha[size_t(y) * W + x], k = colour[size_t(y) * W + x];
                p.at(x, y) = {g * (1 - a) + k * a, g * 0.96f * (1 - a) + k * a, g * 0.9f * (1 - a) + k * a};
            }
            const Plane mask = step(state, lumaPlane(p, 0), r);
            if (f == 13) for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
                const bool text = alpha[size_t(y) * W + x] > 0;
                if (text && colour[size_t(y) * W + x] > 0.5f && mask.at(x, y) > 0) ++textHeld;
                bool nearText = false;
                for (int j = -6; j <= 6 && !nearText; ++j) for (int i = -6; i <= 6 && !nearText; ++i)
                    nearText = x + i >= 0 && y + j >= 0 && x + i < W && y + j < H && alpha[size_t(y + j) * W + x + i] > 0;
                if (!nearText && mask.at(x, y) > 0) ++roadMarked;
            }
        }
        std::printf("  subtitle band: %d of %d text pixels held, %d road pixels marked\n", textHeld, textPixels, roadMarked);
        check(textHeld * 10 >= textPixels * 9, "a line of text standing over a moving road is held");
        check(roadMarked == 0, "and nothing of the road is");
        check(!subtitlesWanted(), "Subtitle Hold is off unless switched on");
        setSubtitlesWanted(true); check(subtitlesWanted() && !wanted(), "its switch is its own"); setSubtitlesWanted(false);
    }
    {   // off unless switched on
        check(!wanted(), "Crosshair Hold is off unless switched on");
        setWanted(true); check(wanted(), "and on when it is"); setWanted(false);
    }
    std::printf("%s (%d failed)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
