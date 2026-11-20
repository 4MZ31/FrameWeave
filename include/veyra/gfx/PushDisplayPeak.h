// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Is the peak brightness a display reports something to scale the HDR tone curve by? Pure.
//
// Windows hands on what the display's own data block says (DXGI_OUTPUT_DESC1). Many displays do
// not put a measurement there but the top of the HDR signal's range, and a curve scaled for that
// starts rolling off the highlights far above anything the picture contains: it does nothing.
// So a report is judged before it is used, by what it says against itself. No list of makes.
//  - Ceiling: the peak is the top of the signal's range (10000 nits), or it is very high (4000 and
//    more) together with a black of practically nothing (under 0.01). A real panel that bright does
//    exist; one that bright whose data block also gives an idealised black is far more often a
//    block that was never filled in. A near-zero black alone says nothing against a report: every
//    OLED has one.
//  - Contradictory: the whole screen brighter than a small area of it, or a black as bright as the peak.
// A report that is not trusted leaves the curve at its reference, exactly as when the display
// reports nothing; it is never replaced by a guessed number. A peak the user set always wins.
// This judges what the driver says. It measures no light.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace veyra::gfx {
enum class DisplayPeakTrust : std::uint8_t {
    Unknown,        // nothing usable reported
    Trusted,
    Ceiling,
    Contradictory,
};
constexpr const char* displayPeakTrustName(DisplayPeakTrust t) {
    return t == DisplayPeakTrust::Trusted ? "trusted" : t == DisplayPeakTrust::Ceiling ? "ceiling"
         : t == DisplayPeakTrust::Contradictory ? "contradictory" : "unknown";
}
inline constexpr double kPeakCeilingNits = 10000.0;     // the top of the PQ range
inline constexpr double kPeakVeryHighNits = 4000.0;
inline constexpr double kIdealBlackNits = 0.01;
// peak, whole-screen and black level in nits; a value that is unknown is 0.
inline DisplayPeakTrust judgeDisplayPeak(double peakNits, double fullFrameNits, double minNits) {
    if (!std::isfinite(peakNits) || peakNits < 100.0 || peakNits > kPeakCeilingNits) return DisplayPeakTrust::Unknown;
    if (!std::isfinite(fullFrameNits) || fullFrameNits < 0) fullFrameNits = 0;
    if (!std::isfinite(minNits) || minNits < 0) minNits = 0;
    if (peakNits >= kPeakCeilingNits) return DisplayPeakTrust::Ceiling;
    if (peakNits >= kPeakVeryHighNits && minNits < kIdealBlackNits) return DisplayPeakTrust::Ceiling;
    // One nit of slack: both numbers are rounded on their way here.
    if (fullFrameNits > peakNits + 1.0) return DisplayPeakTrust::Contradictory;
    if (minNits >= peakNits) return DisplayPeakTrust::Contradictory;
    return DisplayPeakTrust::Trusted;
}
// The display peak the tone curve works with: the user's when set, a trusted report otherwise,
// and 0 (the curve's own reference) when there is neither.
inline unsigned displayPeakForCurve(unsigned manualNits, unsigned reportedNits, bool trusted) {
    if (manualNits != 0) return manualNits;
    return trusted ? std::clamp(reportedNits, 100u, 10000u) : 0u;
}
} // namespace veyra::gfx
