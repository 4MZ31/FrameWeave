// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Dolby Vision profile 8.4 whose colour description says PQ (source/DolbyVision.h, correctHlgBase):
// that one contradiction is read as HLG, and nothing else changes. Every other profile, every other
// compatibility and every stream without the Dolby Vision configuration is routed exactly as before.
#include "veyra/source/DolbyVision.h"
#include <cstdio>
#include <string>
using namespace veyra::source;
using namespace veyra::pipeline;
static int failures = 0;
static void check(bool ok, const std::string& label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str()); if (!ok) ++failures; }
static DolbyVisionInfo stream(int profile, int compatibility, bool baseLayer = true) {
    DolbyVisionInfo d; d.present = true; d.baseLayer = baseLayer; d.rpuDeclared = true; d.profile = uint8_t(profile); d.compatibility = uint8_t(compatibility); return d;
}
static ColorDescription colour(TransferFunction transfer, YuvMatrix matrix = YuvMatrix::BT2020NCL, ColorPrimaries primaries = ColorPrimaries::BT2020) {
    ColorDescription c; c.transfer = transfer; c.matrix = matrix; c.primaries = primaries; return c;
}
int main() {
    {   // The case it is for.
        const auto dv = stream(8, 4);
        auto c = colour(TransferFunction::PQ);
        check(!dv.matches(c), "profile 8.4 described as PQ was refused");
        check(dv.correctHlgBase(c) && c.transfer == TransferFunction::HLG && c.transferAssumed, "it is read as HLG, marked as assumed");
        check(dv.matches(c), "and then plays as an HLG base layer");
        check(!dv.correctHlgBase(c), "a second look changes nothing");
        auto none = colour(TransferFunction::Unknown);
        check(dv.correctHlgBase(none) && dv.matches(none), "no transfer at all is read as HLG too");
        auto right = colour(TransferFunction::HLG);
        check(!dv.correctHlgBase(right) && !right.transferAssumed && dv.matches(right), "a correct HLG description is left alone");
    }
    {   // What it must not touch.
        auto c = colour(TransferFunction::PQ);
        const ColorDescription before = c;
        const auto same = [&](const ColorDescription& x) { return x.transfer == before.transfer && x.transferAssumed == before.transferAssumed; };
        check(!stream(8, 1).correctHlgBase(c) && same(c) && stream(8, 1).matches(c), "profile 8.1 (HDR10 base layer) stays PQ");
        check(!stream(8, 2).correctHlgBase(c) && same(c), "profile 8.2 (SDR base layer) is not touched");
        check(!stream(7, 6).correctHlgBase(c) && same(c) && stream(7, 6).matches(c), "profile 7 stays PQ");
        check(!stream(10, 4).correctHlgBase(c) && same(c), "profile 10 with compatibility 4 is not touched");
        check(!stream(5, 0).correctHlgBase(c) && same(c), "profile 5 is not touched");
        check(!stream(8, 4, false).correctHlgBase(c) && same(c), "nor a stream without a base layer");
        check(!DolbyVisionInfo{}.correctHlgBase(c) && same(c), "nor an ordinary PQ HDR10 file without Dolby Vision");
        for (int compatibility : {0, 3, 5, 6, 7}) check(!stream(8, compatibility).correctHlgBase(c) && same(c), "compatibility " + std::to_string(compatibility) + " is not touched");
    }
    {   // A stream that says something else is not the one the configuration describes: still refused.
        const auto dv = stream(8, 4);
        for (auto transfer : {TransferFunction::BT709, TransferFunction::SRGB}) {
            auto c = colour(transfer);
            check(!dv.correctHlgBase(c) && c.transfer == transfer && !dv.matches(c), "an SDR transfer under profile 8.4 stays refused");
        }
        auto matrix = colour(TransferFunction::PQ, YuvMatrix::BT709);
        check(!dv.correctHlgBase(matrix) && matrix.transfer == TransferFunction::PQ && !dv.matches(matrix), "PQ with a BT.709 matrix stays refused");
        auto primaries = colour(TransferFunction::PQ, YuvMatrix::BT2020NCL, ColorPrimaries::BT709);
        check(!dv.correctHlgBase(primaries) && !dv.matches(primaries), "PQ with BT.709 primaries stays refused");
    }
    {   // The routes themselves are what they were.
        check(stream(8, 4).route() == DolbyBaseLayer::Hlg && stream(8, 1).route() == DolbyBaseLayer::Hdr10 && stream(8, 2).route() == DolbyBaseLayer::Sdr, "profile 8 routes");
        // Profile 5 is converted from its RPU since the 2.2.0 merge (it was refused before): its route is the PQ container.
        check(stream(7, 6).route() == DolbyBaseLayer::Hdr10 && stream(5, 0).route() == DolbyBaseLayer::Hdr10 && stream(8, 4, false).route() == DolbyBaseLayer::Unsupported, "profiles 7 and 5, and no base layer");
        check(DolbyVisionInfo{}.route() == DolbyBaseLayer::None, "no Dolby Vision");
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
