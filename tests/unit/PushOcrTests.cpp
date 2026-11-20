// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Reading the text on the picture: the arithmetic of ui/PushOcr.h. Pure: also builds with g++.
// (The recognition itself is Windows': tests/integration/PushOcrWindowsTests.cpp runs it where a
// language is installed.)
#include "veyra/ui/PushOcr.h"
#include <cstdio>
#include <limits>
using namespace veyra::ui::ocr;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::printf("FAILED line %d: %s\n", __LINE__, #x); } } while (0)
static bool nearly(double a, double b) { return std::fabs(a - b) < 1e-9; }
int main() {
    // The regions the interface offers.
    CHECK(nearly(regionFor(0).w, 1) && nearly(regionFor(0).h, 1) && nearly(regionFor(0).y, 0));
    CHECK(nearly(regionFor(1).y, 0.5) && nearly(regionFor(1).h, 0.5));
    CHECK(nearly(regionFor(2).y + regionFor(2).h, 1.0) && nearly(regionFor(2).h, 1.0 / 3.0));
    CHECK(nearly(regionFor(3).y, 0) && nearly(regionFor(3).h, 0.5));
    CHECK(nearly(regionFor(99).h, 1) && nearly(regionFor(-1).h, 1));

    // A region is always inside the picture and never empty.
    { const auto r = clampRegion({-0.5, 0.25, 2, 2}); CHECK(nearly(r.x, 0) && nearly(r.y, 0.25) && nearly(r.w, 1) && nearly(r.h, 0.75)); }
    { const auto r = clampRegion({0.9, 0.9, 0.5, 0.5}); CHECK(nearly(r.x, 0.9) && nearly(r.w, 0.1) && nearly(r.h, 0.1)); }
    { const auto r = clampRegion({0.5, 0.5, 0, 0}); CHECK(nearly(r.w, 1) && nearly(r.h, 1) && nearly(r.x, 0)); }           // empty: the whole picture
    { const auto r = clampRegion({std::numeric_limits<double>::quiet_NaN(), 0, 1, 1}); CHECK(nearly(r.w, 1) && nearly(r.x, 0)); }
    { const auto r = clampRegion({1.0, 1.0, 0.5, 0.5}); CHECK(nearly(r.w, 1) && nearly(r.h, 1)); }

    // A 4K picture is read as it is; its lower third is cut out of it.
    { const auto p = planFor(3840, 2160, regionFor(0), 10000);
      CHECK(p.sourceX == 0 && p.sourceY == 0 && p.sourceWidth == 3840 && p.sourceHeight == 2160 && p.width == 3840 && p.height == 2160 && p.valid() && !p.scaled()); }
    { const auto p = planFor(3840, 2160, regionFor(2), 10000);
      CHECK(p.sourceY == 1440 && p.sourceHeight == 720 && p.sourceWidth == 3840 && p.width == 3840 && p.height == 720 && !p.scaled()); }
    // Small text: a part lower than 720 pixels is read at twice its size.
    { const auto p = planFor(1920, 1080, regionFor(1), 10000);
      CHECK(p.sourceY == 540 && p.sourceHeight == 540 && p.width == 3840 && p.height == 1080 && p.scaled()); }
    { const auto p = planFor(1280, 720, regionFor(0), 10000); CHECK(p.width == 1280 && p.height == 720); }   // 720 is not under 720
    { const auto p = planFor(1280, 718, regionFor(0), 10000); CHECK(p.width == 2560 && p.height == 1436); }
    // Never larger than the recogniser takes, whatever the picture.
    { const auto p = planFor(7680, 4320, regionFor(0), 4000); CHECK(p.width == 4000 && p.height == 2250 && p.sourceWidth == 7680); }
    { const auto p = planFor(3000, 600, regionFor(0), 4000); CHECK(p.width == 4000 && p.height == 800); }   // doubling would pass the limit
    { const auto p = planFor(20000, 100, regionFor(0), 2000); CHECK(p.width <= 2000 && !p.valid()); }      // what is left is too low to read
    // The part cut out never leaves the picture, and is never empty.
    for (int choice = 0; choice < 4; ++choice)
        for (const auto& [w, h] : {std::pair<uint32_t, uint32_t>{1919, 1079}, {3841, 2161}, {641, 361}, {17, 17}, {1, 1}}) {
            const auto p = planFor(w, h, regionFor(choice), 10000);
            CHECK(p.sourceWidth >= 1 && p.sourceHeight >= 1 && p.sourceX + p.sourceWidth <= w && p.sourceY + p.sourceHeight <= h);
        }
    CHECK(!planFor(0, 0, regionFor(0), 10000).valid());
    CHECK(!planFor(4, 4, regionFor(0), 0).valid());
    CHECK(planFor(1920, 1080, regionFor(0), 0).valid());        // no limit known
    CHECK(planFor(7680, 4320, regionFor(0), 0).valid());        // the largest picture PushFPS makes
    CHECK(!planFor(16000, 9000, regionFor(0), 10000).valid());  // more than is ever held in memory

    // Half floats, as an HDR frame is saved.
    CHECK(halfToFloat(0x0000) == 0.0f && halfToFloat(0x3C00) == 1.0f && halfToFloat(0x4000) == 2.0f && halfToFloat(0x3800) == 0.5f);
    CHECK(halfToFloat(0xBC00) == -1.0f && halfToFloat(0x7BFF) == 65504.0f && halfToFloat(0x0001) == std::ldexp(1.0f, -24));
    CHECK(std::isinf(halfToFloat(0x7C00)) && std::isnan(halfToFloat(0x7C01)));
    for (uint32_t bits = 0; bits < 0x7BFF; ++bits) CHECK(halfToFloat(uint16_t(bits + 1)) > halfToFloat(uint16_t(bits)));   // order of patterns = order of values
    for (const float v : {0.0f, 0.001f, 0.1f, 0.5f, 1.0f, 2.5375f, 12.5f, 125.0f, 65504.0f}) {
        CHECK(std::fabs(halfToFloat(floatToHalf(v)) - v) <= v * 0.001f);
        CHECK(std::fabs(halfToFloat(floatToHalf(-v)) + v) <= v * 0.001f);
    }
    CHECK(floatToHalf(1.0f) == 0x3C00 && floatToHalf(2.0f) == 0x4000 && floatToHalf(1.0e9f) == 0x7BFF && floatToHalf(std::nanf("")) == 0);
    for (uint32_t bits = 0; bits <= 0x7BFF; bits += 7) CHECK(floatToHalf(halfToFloat(uint16_t(bits))) == bits);

    // The brightest value of a part: never under reference white, never more than a half holds.
    CHECK(hdrPeak(0) == kHdrWhite && hdrPeak(floatToHalf(0.2f)) == kHdrWhite);
    CHECK(std::fabs(hdrPeak(floatToHalf(12.5f)) - 12.5f) < 0.01f);
    CHECK(hdrPeak(0x7C00) == 65504.0f && hdrPeak(0xFFFF) == 65504.0f);
    // The darkest value that becomes black: what the part has, at most a quarter of the brightest.
    CHECK(hdrFloor(0, 10.0f) == 0.0f && hdrFloor(0x8000, 10.0f) == 0.0f);
    CHECK(std::fabs(hdrFloor(floatToHalf(1.0f), 10.0f) - 1.0f) < 0.001f);
    CHECK(hdrFloor(floatToHalf(3.0f), 10.0f) == 2.5f && hdrFloor(floatToHalf(9.99f), 10.0f) == 2.5f);
    CHECK(hdrFloor(floatToHalf(3.0f), 0.0f) == kHdrWhite * 0.25f);   // a peak that makes no sense is reference white
    // An HDR value as a byte: the darkest is 0, the brightest is 255, nothing between is cut off.
    for (const float peak : {kHdrWhite, 12.5f, 50.0f, 125.0f})
        for (const float low : {0.0f, 0.05f, peak * 0.25f}) {
            CHECK(hdrByte(0, low, peak) == 0 && hdrByte(-3, low, peak) == 0 && hdrByte(std::nanf(""), low, peak) == 0 && hdrByte(low, low, peak) == 0);
            CHECK(hdrByte(peak, low, peak) == 255 && hdrByte(peak * 4, low, peak) == 255 && hdrByte(std::numeric_limits<float>::infinity(), low, peak) == 255);
            int before = -1;
            for (float v = 0; v <= peak; v += peak / 4096.0f) { const int now = hdrByte(v, low, peak); CHECK(now >= before); before = now; }
            // Text and what is behind it, 2 to 1, anywhere from a tenth of white up: at least 16 of 255 apart.
            for (float v = std::max(kHdrWhite * 0.1f, low); v * 2 <= peak; v *= 1.19f)
                CHECK(int(hdrByte(v * 2, low, peak)) - int(hdrByte(v, low, peak)) >= 16);
        }
    CHECK(hdrByte(1.0f, 0.0f, 0.0f) == hdrByte(1.0f, 0.0f, kHdrWhite));          // a peak that makes no sense
    CHECK(hdrByte(1.0f, -5.0f, 10.0f) == hdrByte(1.0f, 0.0f, 10.0f));            // a low that makes no sense
    CHECK(hdrByte(6.0f, 9.0f, 10.0f) == hdrByte(6.0f, 2.5f, 10.0f));             // a low above a quarter of the peak
    // The case the usual way loses: grey text at 240 nits on a panel of 800, both above white.
    { const float peak = hdrPeak(floatToHalf(10.0f)), low = hdrFloor(floatToHalf(3.0f), peak);
      CHECK(hdrByte(10.0f, low, peak) == 255 && hdrByte(3.0f, low, peak) < 60); }
    // Ordinary text at reference white on a dark picture that also has a 4000 nit highlight.
    { const float peak = hdrPeak(floatToHalf(50.0f)), low = hdrFloor(0, peak);
      CHECK(int(hdrByte(kHdrWhite, low, peak)) - int(hdrByte(kHdrWhite / 4, low, peak)) >= 40); }
    // Faint text on a bright flat panel (9 on 10): told apart, though the panel is not stretched to black.
    { const float peak = 10.0f, low = hdrFloor(floatToHalf(9.0f), peak);
      CHECK(int(hdrByte(10.0f, low, peak)) - int(hdrByte(9.0f, low, peak)) >= 12 && hdrByte(9.0f, low, peak) > 128); }

    // The lines as one text.
    {
        const std::vector<Line> lines{{L"  New game ", 0, 0, 0, 0}, {L"", 0, 0, 0, 0}, {L"   ", 0, 0, 0, 0}, {L"Options", 0, 0, 0, 0}};
        CHECK(joinLines(lines) == L"New game\nOptions");
        CHECK(joinLines({}).empty());
    }
    if (failures) { std::printf("%d failed\n", failures); return 1; }
    std::printf("PushOcrTests: all passed\n");
    return 0;
}
