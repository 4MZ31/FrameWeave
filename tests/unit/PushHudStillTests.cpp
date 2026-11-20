// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The still rule (pipeline/PushHudStill.h): what stands still in front of a moving picture keeps
// its real pixels in generated frames, and nothing else does. Here the rule itself, on pictures
// described as "which pixels are the same in both real frames". The shaders are compared with
// this same function on a graphics card in tests/integration/PushGpuTests.cpp. Pure: builds anywhere.
#include "veyra/pipeline/PushHudStill.h"
#include <cstdio>
#include <string>
using namespace veyra::pipeline::hudstill;
static int failures = 0;
static void check(bool ok, const std::string& label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str()); if (!ok) ++failures; }
struct Picture {
    unsigned w, h; std::vector<uint8_t> same;
    Picture(unsigned w_, unsigned h_, bool still) : w(w_), h(h_), same(size_t(w_) * h_, still ? 1 : 0) {}
    void set(unsigned x, unsigned y, bool s) { if (x < w && y < h) same[size_t(y) * w + x] = s ? 1 : 0; }
    void box(unsigned x0, unsigned y0, unsigned x1, unsigned y1, bool s) { for (unsigned y = y0; y < y1; ++y) for (unsigned x = x0; x < x1; ++x) set(x, y, s); }
    std::vector<uint8_t> keep() const { return decide(same, w, h); }
};
static unsigned count(const std::vector<uint8_t>& v) { unsigned n = 0; for (auto b : v) n += b ? 1u : 0u; return n; }
int main() {
    const unsigned W = 320, H = 192;
    {   // A HUD in front of a moving world: thin strokes, a filled bar, a small block.
        Picture p(W, H, false);
        for (unsigned y = 20; y < 36; ++y) p.set(40, y, true);            // an upright stroke, one pixel wide
        for (unsigned x = 60; x < 90; ++x) p.set(x, 28, true);            // a level stroke
        for (unsigned i = 0; i < 14; ++i) p.set(100 + i, 20 + i, true);   // a slanted stroke
        p.box(40, 150, 140, 158, true);                                   // a bar
        p.box(250, 30, 262, 42, true);                                    // an icon
        const auto keep = p.keep();
        // A stroke one pixel wide has little company at its very end, as a chance match in a moving
        // texture has: its last two pixels are left to frame generation. (Strokes two pixels wide, and
        // strokes that meet others, are kept whole.)
        bool strokes = true;
        for (unsigned y = 22; y < 34; ++y) strokes = strokes && keep[size_t(y) * W + 40];
        for (unsigned x = 62; x < 88; ++x) strokes = strokes && keep[size_t(28) * W + x];
        check(strokes, "upright and level strokes one pixel wide are kept, up to their last two pixels");
        check(!keep[size_t(20) * W + 40] && !keep[size_t(21) * W + 40] && !keep[size_t(35) * W + 40], "those last two at each end are not");
        Picture wide(W, H, false);
        wide.box(40, 20, 42, 26, true);                                   // a short stroke two pixels wide
        const auto kept = wide.keep();
        bool whole = true; for (unsigned y = 20; y < 26; ++y) for (unsigned x = 40; x < 42; ++x) whole = whole && kept[size_t(y) * W + x];
        check(whole, "a short stroke two pixels wide is kept whole");
        unsigned slanted = 0; for (unsigned i = 0; i < 14; ++i) slanted += keep[size_t(20 + i) * W + 100 + i];
        check(slanted == 10, "a slanted stroke is kept up to its last two pixels (" + std::to_string(slanted) + " of 14)");
        bool bar = true; for (unsigned y = 150; y < 158; ++y) for (unsigned x = 40; x < 140; ++x) bar = bar && keep[size_t(y) * W + x];
        check(bar, "a filled bar is kept whole");
        bool icon = true; for (unsigned y = 30; y < 42; ++y) for (unsigned x = 250; x < 262; ++x) icon = icon && keep[size_t(y) * W + x];
        check(icon, "a small block is kept whole");
        bool only = true; for (size_t i = 0; i < keep.size(); ++i) only = only && (!keep[i] || p.same[i]);
        check(only, "and nothing that moves is touched");
    }
    {   // Chance matches in a moving texture have no company.
        Picture p(W, H, false);
        for (unsigned y = 5; y < H; y += 11) for (unsigned x = 3; x < W; x += 13) p.set(x, y, true);     // single pixels
        for (unsigned y = 8; y < H; y += 23) for (unsigned x = 9; x < W; x += 29) { p.set(x, y, true); p.set(x + 1, y, true); }   // pairs
        check(count(p.keep()) == 0, "single pixels and pairs that happen to match are left to frame generation");
    }
    {   // A still picture (a menu, a still camera): nothing is touched, whatever crosses it.
        Picture p(W, H, true);
        check(count(p.keep()) == 0, "a picture that stands still everywhere: nothing kept");
        p.box(150, 80, 170, 100, false); p.box(210, 80, 230, 100, false);   // a ball's old and new place; in between both frames show the world
        const auto keep = p.keep();
        check(count(keep) == 0, "a ball crossing a still world: its path is not touched");
    }
    {   // A larger still region with the world moving round it (a character standing still, the sky beside
        // a moving head, a wall behind a moving thing): since 4.1 nothing of it is kept, its border least of
        // all. 4.0 kept that border, and on a real card that broke frame generation's soft trail round
        // everything that moved into hard pieces.
        Picture p(W, H, false);
        p.box(110, 40, 210, 192, true);
        const auto keep = p.keep();
        // All that is left is the block at each of its two corners, where the region is under a quarter of
        // what surrounds the block (2 x 8 pixels each).
        unsigned border = 0; for (unsigned y = 56; y < 192; ++y) border += keep[size_t(y) * W + 110] + keep[size_t(y) * W + 111] + keep[size_t(y) * W + 208] + keep[size_t(y) * W + 209];
        check(border == 0 && count(keep) <= 32, "a still region 100 pixels wide: its border is left to frame generation (" + std::to_string(count(keep)) + " pixels kept, at its two corners)");
        Picture sky(W, H, true);
        sky.box(120, 60, 200, 192, false);               // a head and shoulders moving in front of a still sky
        check(count(sky.keep()) == 0, "the still picture beside something that moves is left to frame generation");
        Picture halfMoving(W, H, false);
        halfMoving.box(0, 0, W, 90, true);               // the top half stands still, the bottom half moves
        check(count(halfMoving.keep()) == 0, "the border between a still half and a moving half is left alone");
    }
    {   // An island is under a quarter of its surrounding (80 x 80 pixels at this size).
        Picture small(160, 160, false);
        small.box(64, 64, 96, 96, true);                 // 32 x 32 = 16 % of 80 x 80
        const auto keep = small.keep();
        bool whole = true; for (unsigned y = 64; y < 96; ++y) for (unsigned x = 64; x < 96; ++x) whole = whole && keep[size_t(y) * 160 + x];
        check(whole, "a still block of 32 x 32 in a moving picture is an island: kept whole");
        Picture big(160, 160, false);
        big.box(48, 48, 112, 112, true);                 // 64 x 64 = 64 % of 80 x 80
        check(count(big.keep()) == 0, "a still block of 64 x 64 is no island: left alone");
        Picture line(160, 160, false);
        line.box(0, 80, 160, 100, true);                 // a band 20 pixels high across the picture: exactly a quarter
        check(count(line.keep()) == 0, "exactly a quarter still is not under a quarter: left alone");
        Picture thinner(160, 160, false);
        thinner.box(0, 80, 160, 99, true);               // 19 pixels high: under a quarter
        check(count(thinner.keep()) == 160u * 19u, "one row less is under a quarter: kept");
    }
    {   // The surrounding reaches further in a larger picture.
        using veyra::pipeline::hudstill::reachFor;
        check(reachFor(192) == 2 && reachFor(720) == 2 && reachFor(1080) == 3 && reachFor(1440) == 3 && reachFor(2160) == 5 && reachFor(4320) == 6 && reachFor(0) == 2,
              "2 blocks each way up to 720 high, 3 at 1080 and 1440, 5 at 2160, never more than 6");
        // A block of 40 x 40 is a quarter of 80 x 80 (no island with a reach of 2) and 5 % of 176 x 176.
        std::vector<uint8_t> same(size_t(400) * 400, 0);
        for (unsigned y = 176; y < 216; ++y) for (unsigned x = 176; x < 216; ++x) same[size_t(y) * 400 + x] = 1;
        check(count(decide(same, 400, 400, 2)) == 0 && count(decide(same, 400, 400, 5)) == 1600, "the same block is an island where the surrounding reaches further");
    }
    {   // Edges of the picture and odd sizes.
        Picture p(37, 21, false);
        p.box(0, 0, 37, 2, true); p.box(35, 0, 37, 21, true);
        const auto keep = p.keep();
        check(keep.size() == size_t(37) * 21 && keep[0] && keep[36] && keep[size_t(20) * 37 + 36], "strokes along the picture's edge are kept, in a picture that is no multiple of a block");
        check(decide({}, 0, 0).empty() && count(decide(std::vector<uint8_t>(10, 1), 4, 4)) == 0, "a size that does not fit its data keeps nothing");
    }
    {   // The switch.
        check(!wanted(), "off unless switched on");
        setWanted(true); check(wanted(), "on"); setWanted(false); check(!wanted(), "and off again");
        check(kPairStill > 2.0f / 255.0f && kPairStill < 3.0f / 255.0f, "two steps of 255 are still the same, three are not");
    }
    {   // A picture that is no multiple of the blocks: the cut blocks at the right and the bottom count
        // only the pixels they have. A stroke in the cut corner, in a surrounding that moves, is kept;
        // the same stroke with the cut blocks standing still around it is not.
        const unsigned w = 167, h = 91;                       // 10 whole blocks and 7 px; 5 whole blocks and 11 px
        std::vector<uint8_t> same(size_t(w) * h, 0);
        for (unsigned y = 82; y < 90; ++y) { same[size_t(y) * w + 163] = 1; same[size_t(y) * w + 164] = 1; }
        auto keep = decide(same, w, h);
        check(keep[size_t(85) * w + 163] && keep[size_t(85) * w + 164], "in a cut block at the picture's corner: a stroke in a moving surrounding is kept");
        for (unsigned y = 40; y < h; ++y) for (unsigned x = 120; x < w; ++x) same[size_t(y) * w + x] = 1;
        keep = decide(same, w, h);
        check(!keep[size_t(85) * w + 163], "in a cut block: a still surrounding leaves it alone");
        // Exactly a quarter still is no island: the test is under a quarter (counted in whole pixels).
        const unsigned w2 = 80, h2 = 80;                      // 5 x 5 blocks: the middle column of blocks has all 25 as its surrounding
        std::vector<uint8_t> quarter(size_t(w2) * h2, 0);
        for (unsigned y = 0; y < h2; ++y) for (unsigned x = 30; x < 50; ++x) quarter[size_t(y) * w2 + x] = 1;   // 20 of 80 columns, through the middle
        keep = decide(quarter, w2, h2);
        size_t any = 0; for (auto k : keep) any += k;
        check(any == 0, "a surrounding that is exactly a quarter still is left alone");
        quarter[size_t(10) * w2 + 49] = 0;                     // one pixel fewer: under a quarter now
        keep = decide(quarter, w2, h2);
        any = 0; for (auto k : keep) any += k;
        check(any > 0, "one pixel under a quarter: it counts as an island");
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
