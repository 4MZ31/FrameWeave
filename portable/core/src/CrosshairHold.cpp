// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#include "veyra/portable/CrosshairHold.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace veyra::portable {
namespace {
using namespace pipeline::overlay;

uint8_t byteOf(float v) { return uint8_t(std::clamp(std::lround(v * 255.0f), 0L, 255L)); }

Bgra toBgra(const Picture& p) {
    Bgra out(p.size, p.size);
    for (int y = 0; y < p.size; ++y)
        for (int x = 0; x < p.size; ++x) {
            const Colour& c = p.at(x, y);
            uint8_t* o = out.pixel(x, y);
            o[0] = byteOf(c.b); o[1] = byteOf(c.g); o[2] = byteOf(c.r); o[3] = 255;
        }
    return out;
}

// The least and the greatest of the `flank` pixels on each side of a pixel whose flanks all lie in the zone,
// along one direction. A bright bar (the rule's barAt with sign +1) is `here - least >= kContrast` on both
// sides; a dark one (sign -1) is `(-here) - (-greatest)`, which is `greatest - here` to the last bit. So one
// pass over the flanks answers both kinds of a direction.
struct Flanks { float leastA, leastB, greatestA, greatestB; };
inline Flanks flanksOf(const float* at, int step, int flank) {
    Flanks f{1e30f, 1e30f, -1e30f, -1e30f};
    for (int i = 1; i <= flank; ++i) {
        const float a = at[-i * step], b = at[i * step];
        f.leastA = std::min(f.leastA, a); f.greatestA = std::max(f.greatestA, a);
        f.leastB = std::min(f.leastB, b); f.greatestB = std::max(f.greatestB, b);
    }
    return f;
}

Picture pictureOf(const Bgra& zone) {
    Picture out(zone.width);
    for (int y = 0; y < zone.height; ++y) {
        const uint8_t* p = zone.pixel(0, y);
        for (int x = 0; x < zone.width; ++x, p += 4) out.at(x, y) = {float(p[2]) / 255.0f, float(p[1]) / 255.0f, float(p[0]) / 255.0f};
    }
    return out;
}
}  // namespace

// The rule's step(), statement for statement, with three savings that change no result (the test holds the
// two against each other, pixel for pixel, counts and memory included): whether the picture moves near a pixel
// is worked out once a block and not once a pixel; a pixel whose flanks lie inside the zone is judged without
// the bounds questions; and the mask is drawn from the accepted pixels outwards, where the rule asks every
// pixel of the zone whether an accepted one lies near it. On a picture without a crosshair that last part was
// most of the time.
Plane stepFast(State& s, const Plane& luma, const Rule& r) {
    const int n = s.size, blocks = n / kBlock;
    const std::vector<unsigned char> flags = s.hasBefore ? movingBlocks(s.before, luma) : std::vector<unsigned char>(size_t(blocks) * blocks, 0);
    s.moving = s.hasBefore && zoneMoves(flags);
    std::vector<unsigned char> movesHere(size_t(blocks) * blocks, 0);
    if (s.moving)
        for (int by = 0; by < blocks; ++by)
            for (int bx = 0; bx < blocks; ++bx) movesHere[size_t(by) * blocks + bx] = movesNear(flags, blocks, bx * kBlock, by * kBlock) ? 1 : 0;
    const float* l = luma.v.data();
    // Kinds 0 and 2 are the bright and the dark bar with flanks left and right, 1 and 3 with flanks above and
    // below (kindSign, kindRunsAcross): one pass over a direction's flanks serves its two kinds.
    for (int direction = 0; direction < 2; ++direction) {
        const int bright = direction, dark = direction + 2;
        const bool across = kindRunsAcross(bright);
        const int step = across ? n : 1;
        float* countBright = s.count[bright].v.data();
        float* countDark = s.count[dark].v.data();
        for (int y = 0; y < n; ++y) {
            const int by = std::min(y / kBlock, blocks - 1);
            const bool rowInside = across ? (y >= r.flank && y + r.flank < n) : true;
            for (int x = 0; x < n; ++x) {
                const size_t at = size_t(y) * n + x;
                const bool inside = across ? rowInside : (x >= r.flank && x + r.flank < n);
                bool isBright, isDark;
                if (inside) {
                    const Flanks f = flanksOf(l + at, step, r.flank);
                    const float here = l[at];
                    isBright = here - f.leastA >= kContrast && here - f.leastB >= kContrast;
                    isDark = f.greatestA - here >= kContrast && f.greatestB - here >= kContrast;
                } else {
                    isBright = barAt(luma, x, y, bright, r);
                    isDark = barAt(luma, x, y, dark, r);
                }
                const bool moving = s.moving && movesHere[size_t(by) * blocks + std::min(x / kBlock, blocks - 1)] != 0;
                countBright[at] = nextCount(countBright[at], isBright, moving, r);
                countDark[at] = nextCount(countDark[at], isDark, moving, r);
            }
        }
    }
    Plane accepted(n);
    std::vector<int> acceptedAt;
    for (int k = 0; k < kKinds; ++k) {
        Plane memory(n);
        const float* count = s.count[k].v.data();
        const float* old = s.memory[k].v.data();
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const size_t at = size_t(y) * n + x;
                Decision d;
                if (isBar(count[at])) d = judge(s.count[k], s.memory[k], x, y, k, r);
                if (d.accepted && !(accepted.v[at] > 0)) { accepted.v[at] = 1; acceptedAt.push_back(int(at)); }
                memory.v[at] = d.found ? float(r.memory) : std::max(old[at] - 1.0f, 0.0f);
            }
        s.memory[k] = std::move(memory);
    }
    Plane mask(n);
    if (!acceptedAt.empty()) {
        int x0 = n, y0 = n, x1 = -1, y1 = -1;
        for (const int at : acceptedAt) {
            const int x = at % n, y = at / n;
            x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
            // every pixel within `grow` of an accepted one (the rule asks it the other way round: the same set)
            for (int j = -r.grow; j <= r.grow; ++j)
                for (int i = -r.grow; i <= r.grow; ++i)
                    if (within(i, j, r.grow) && mask.inside(x + i, y + j)) mask.at(x + i, y + j) = 1.0f;
        }
        // the inside of a small closed shape: only a pixel with accepted ones on all four sides, so only
        // between the outermost accepted pixels
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                if (!(mask.at(x, y) > 0) && enclosed(accepted, x, y, r.hole)) mask.at(x, y) = 1.0f;
    }
    s.before = luma; s.hasBefore = true;
    return mask;
}

void CrosshairHold::reset() {
    state_ = State(0);
    width_ = height_ = 0;
}

std::shared_ptr<const HoldFrame> CrosshairHold::frame(const Bgra& image) {
    if (image.empty()) return nullptr;
    const Zone zone = zoneFor(uint32_t(image.width), uint32_t(image.height));
    if (zone.size == 0) { reset(); return nullptr; }
    const int n = int(zone.size);
    // Another picture size: what was counted belongs to other pixels.
    if (image.width != width_ || image.height != height_ || state_.size != n) {
        state_ = State(n);
        width_ = image.width;
        height_ = image.height;
    }
    const Rule rule = ruleFor(uint32_t(image.height));
    auto out = std::make_shared<HoldFrame>();
    out->x = int(zone.x); out->y = int(zone.y); out->size = n;
    out->zone = Bgra(n, n);
    Plane luma(n);
    for (int y = 0; y < n; ++y) {
        const uint8_t* p = image.pixel(out->x, out->y + y);
        std::memcpy(out->zone.pixel(0, y), p, size_t(n) * 4);
        // the rule's lumaOf of the pixel as 0..1, written out (the same operations in the same order)
        for (int x = 0; x < n; ++x, p += 4)
            luma.at(x, y) = 0.2126f * (float(p[2]) / 255.0f) + 0.7152f * (float(p[1]) / 255.0f) + 0.0722f * (float(p[0]) / 255.0f);
    }
    out->mask = stepFast(state_, luma, rule);
    for (const float v : out->mask.v) if (v > 0) ++out->masked;
    if (out->masked > 0) out->filled = fill(pictureOf(out->zone), out->mask, rule);
    return out;
}

std::shared_ptr<const HoldPair> CrosshairHold::pair(const HoldFrame& a, const HoldFrame& b) {
    if (a.size <= 0 || a.size != b.size || a.x != b.x || a.y != b.y) return nullptr;
    if (a.masked == 0 && b.masked == 0) return nullptr;
    const Picture originalA = pictureOf(a.zone), originalB = pictureOf(b.zone);
    const Picture& fa = a.masked > 0 ? a.filled : originalA;
    const Picture& fb = b.masked > 0 ? b.filled : originalB;
    const int n = a.size;
    auto out = std::make_shared<HoldPair>();
    out->x = a.x; out->y = a.y; out->size = n;
    // A frame with nothing masked is given as it is: its own bytes, not a round trip through floats.
    out->filledA = a.masked > 0 ? toBgra(fa) : a.zone;
    out->filledB = b.masked > 0 ? toBgra(fb) : b.zone;
    out->shows = Gray(n, n, 255);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            if (!(a.mask.at(x, y) > 0 || b.mask.at(x, y) > 0)) continue;
            const float m = showsThrough(originalA, originalB, fa, fb, a.mask, b.mask, x, y);
            out->shows.at(x, y) = uint8_t(std::clamp(std::lround(m * float(kShowsFull)), 0L, long(kShowsFull)));
            ++out->held;
        }
    return out;
}

}  // namespace veyra::portable
