#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS: reading the text that is on the picture (a menu in another language, a line of dialogue),
// so it can be copied or translated.
//
// Only pixels PushFPS already has are read: one frame of the picture it shows, taken when the user
// asks. Nothing is injected into a game, no memory of another program is read, nothing runs in the
// background, and nothing leaves the PC for the reading itself: the recognition is Windows' own
// (Windows.Media.Ocr), with the languages installed on this PC. The frame is written to PushFPS's
// data folder, read, and deleted.
//
// This header has no Windows and no Qt in it: the shapes, and the arithmetic that is tested with g++
// (tests/unit/PushOcrTests.cpp). The recognition itself is src/ui/PushOcrWinrt.cpp, kept in a file
// of its own without Qt (Qt's `emit` and `slots` are macros).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace veyra::ui::ocr {

// A part of the picture, in shares of its width and height.
struct Region { double x = 0, y = 0, w = 1, h = 1; };
// A line that was read: its text and where it is, in shares of the region that was read.
struct Line { std::wstring text; double x = 0, y = 0, w = 0, h = 0; };
struct Result {
    bool ok = false;
    std::wstring error;      // a short word for the interface: "no-language", "no-file", "failed"
    std::wstring language;   // the language tag Windows read it with
    std::vector<Line> lines;
    uint32_t width = 0, height = 0;   // the size the part was read at
    bool hdr = false;                 // the picture was an HDR one
};
struct Language { std::wstring tag, name; };

// The regions the interface offers: 0 the whole picture, 1 its lower half, 2 its lower third (where
// subtitles and dialogue boxes are), 3 its upper half. Anything else is the whole picture.
constexpr Region regionFor(int choice) {
    return choice == 1 ? Region{0, 0.5, 1, 0.5} : choice == 2 ? Region{0, 2.0 / 3.0, 1, 1.0 / 3.0} : choice == 3 ? Region{0, 0, 1, 0.5} : Region{};
}

// A region that lies inside the picture and is not empty (a value that is not a number is the whole picture).
inline Region clampRegion(Region r) {
    if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.w) || !std::isfinite(r.h)) return {};
    r.x = std::clamp(r.x, 0.0, 1.0); r.y = std::clamp(r.y, 0.0, 1.0);
    r.w = std::clamp(r.w, 0.0, 1.0 - r.x); r.h = std::clamp(r.h, 0.0, 1.0 - r.y);
    if (r.w < 0.01 || r.h < 0.01) return {};
    return r;
}

// What is cut out of a picture and at what size it is read.
struct Plan {
    uint32_t sourceX = 0, sourceY = 0, sourceWidth = 0, sourceHeight = 0;   // the part cut out of the picture
    uint32_t width = 0, height = 0;                                         // the size that part is read at
    bool valid() const { return sourceWidth > 0 && sourceHeight > 0 && width >= 16 && height >= 16; }
    bool scaled() const { return width != sourceWidth || height != sourceHeight; }
};
// Small text reads badly: a part lower than `kSmall` pixels is read at twice its size. Nothing is
// ever larger than the recogniser takes (`maxDimension`, 0 = no limit known): a part that is larger
// is made smaller to fit. A picture of more than `kMaxPixels` is not read at all (8K is the largest
// PushFPS makes; the pixels are held in memory while they are read).
inline constexpr uint32_t kSmall = 720;
inline constexpr uint64_t kMaxPixels = 7680ull * 4320ull;
inline Plan planFor(uint32_t width, uint32_t height, Region region, uint32_t maxDimension) {
    Plan p;
    if (width == 0 || height == 0 || uint64_t(width) * uint64_t(height) > kMaxPixels) return p;
    region = clampRegion(region);
    p.sourceX = std::min(uint32_t(std::floor(region.x * width)), width - 1);
    p.sourceY = std::min(uint32_t(std::floor(region.y * height)), height - 1);
    p.sourceWidth = std::clamp(uint32_t(std::round(region.w * width)), uint32_t(1), width - p.sourceX);
    p.sourceHeight = std::clamp(uint32_t(std::round(region.h * height)), uint32_t(1), height - p.sourceY);
    double scale = p.sourceHeight < kSmall ? 2.0 : 1.0;
    if (maxDimension > 0) {
        const double longest = double(std::max(p.sourceWidth, p.sourceHeight)) * scale;
        if (longest > double(maxDimension)) scale *= double(maxDimension) / longest;
    }
    p.width = uint32_t(std::max(1.0, std::round(double(p.sourceWidth) * scale)));
    p.height = uint32_t(std::max(1.0, std::round(double(p.sourceHeight) * scale)));
    if (maxDimension > 0) { p.width = std::min(p.width, maxDimension); p.height = std::min(p.height, maxDimension); }
    return p;
}

// --- An HDR picture ---------------------------------------------------------------------------
// With an HDR output PushFPS saves a frame as linear half floats in which 1 is 80 nits. Brought to
// eight bits the usual way, everything brighter than white becomes the same white, and text on a
// bright panel is gone. So it is brought down with a logarithm instead, from the darkest to the
// brightest value the part read really has (the darkest counted as at most a quarter of the
// brightest, so a picture that is one flat colour is not stretched into noise): nothing is ever cut
// off, and a step of 2 to 1 between text and what is behind it keeps a good distance wherever in the
// range it lies (the unit test holds that to at least 16 of 255 from a tenth of white upwards,
// whatever the brightest pixel is). The result is a picture for the recogniser, not one to look at.
inline constexpr float kHdrWhite = 203.0f / 80.0f;   // reference white of an HDR picture, in the file's units
inline constexpr float kHdrCurve = 16.0f;

// One half float as a float (IEEE 754 binary16).
inline float halfToFloat(uint16_t bits) {
    const uint32_t exponent = (bits >> 10) & 31u, mantissa = bits & 1023u;
    float value;
    if (exponent == 0) value = std::ldexp(float(mantissa), -24);
    else if (exponent == 31) value = mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    else value = std::ldexp(float(mantissa | 1024u), int(exponent) - 25);
    return (bits & 0x8000u) ? -value : value;
}
// And back, to the nearest (what cannot be held becomes the largest half; not a number becomes 0).
inline uint16_t floatToHalf(float value) {
    if (!(value == value)) return 0;
    uint16_t sign = 0;
    if (value < 0) { sign = 0x8000u; value = -value; }
    if (value >= 65504.0f) return uint16_t(sign | 0x7BFFu);
    if (value < std::ldexp(1.0f, -25)) return sign;
    int e = 0;
    const float m = std::frexp(value, &e);   // value = m * 2^e, m in [0.5, 1)
    int exponent = e + 14;
    if (exponent <= 0) return uint16_t(sign | uint16_t(std::lround(std::ldexp(value, 24))));
    int mantissa = int(std::lround((m * 2.0f - 1.0f) * 1024.0f));
    if (mantissa == 1024) { mantissa = 0; ++exponent; }
    if (exponent >= 31) return uint16_t(sign | 0x7BFFu);
    return uint16_t(sign | uint16_t(exponent << 10) | uint16_t(mantissa));
}
// The brightest value of a part, from the largest bit pattern met among its half floats that is a
// positive number (for those the order of the patterns is the order of the values): never under
// reference white, so a dark picture is not stretched into noise.
inline float hdrPeak(uint16_t largestPositiveBits) {
    const float value = halfToFloat(uint16_t(std::min<uint32_t>(largestPositiveBits & 0x7FFFu, 0x7BFFu)));
    return std::max(kHdrWhite, value);
}
// The darkest value of a part that becomes black, from the smallest such pattern: at most a quarter
// of the brightest.
inline float hdrFloor(uint16_t smallestPositiveBits, float peak) {
    if (!(peak >= kHdrWhite)) peak = kHdrWhite;
    const float value = halfToFloat(uint16_t(std::min<uint32_t>(smallestPositiveBits & 0x7FFFu, 0x7BFFu)));
    return std::clamp(value, 0.0f, peak * 0.25f);
}
// One channel's value as a byte, for a part whose darkest value is `low` and brightest `peak`.
inline uint8_t hdrByte(float value, float low, float peak) {
    if (!(value == value)) return 0;
    if (!(peak >= kHdrWhite)) peak = kHdrWhite;
    if (!(low >= 0)) low = 0;
    if (low > peak * 0.25f) low = peak * 0.25f;
    if (value <= low) return 0;
    if (value >= peak) return 255;
    const auto curve = [](double v) { return std::log1p(double(kHdrCurve) * v / double(kHdrWhite)); };
    const double y = (curve(value) - curve(low)) / (curve(peak) - curve(low));
    return uint8_t(std::clamp(std::lround(y * 255.0), 0l, 255l));
}

// The lines as one text, top to bottom, one line each. A line made of nothing but spaces is left out.
inline std::wstring joinLines(const std::vector<Line>& lines) {
    std::wstring out;
    for (const auto& line : lines) {
        const auto first = line.text.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos) continue;
        const auto last = line.text.find_last_not_of(L" \t\r\n");
        if (!out.empty()) out += L'\n';
        out.append(line.text, first, last - first + 1);
    }
    return out;
}

// --- Windows' part (src/ui/PushOcrWinrt.cpp) ---------------------------------------------------
// The part of a picture file that is read, as rows of blue, green, red and 255, eight bits each.
struct Picture {
    uint32_t width = 0, height = 0;
    uint32_t fileWidth = 0, fileHeight = 0;   // the whole picture's size in the file
    bool hdr = false;                         // it was linear half floats, brought down as described above
    std::vector<uint8_t> bgra;
};
// Cuts `region` out of a picture file (PNG, BMP, JPEG XR: whatever Windows decodes) at the size the
// recogniser is to read it. `error` is "no-file" or "failed". Never throws.
bool loadPicture(const std::wstring& path, Region region, uint32_t maxDimension, Picture& picture, std::wstring& error);
// The largest side the recogniser takes (0 when Windows does not say).
uint32_t maxDimension();
// The languages Windows can read on this PC (empty when none is installed). Never throws.
std::vector<Language> languages();
// Reads the text in a picture file. `languageTag` empty = the user's own languages, in Windows'
// order. Waits for the result: call it from a worker thread, never from the interface's. Never throws.
Result readFile(const std::wstring& path, const std::wstring& languageTag, Region region);

}
