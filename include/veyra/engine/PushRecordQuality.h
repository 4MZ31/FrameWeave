#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The recorder's standard bitrate, and the share of it a recording asks for (PushFPS 4.0).
//
// About 0.3 bit per pixel for H.264 (HEVC about two thirds of it), so fast game motion stays sharp
// (0.15 was soft: field report 2026-11-16). Frames above 60 a second count half: generated frames
// sit between two real ones and cost the encoder less. 1080p60 about 37 Mbps, 1080p120 56,
// 1440p60 66, 4K60 149 (H.264). At 100 % this is, number for number, the rule the recorder always
// had (it stood in LiveRecorder.cpp; the test keeps a copy).
//
// The record dialog (PushRecordDialog.qml, PushRecord.js) offers "smaller files" at half of it
// (preference pushRecordBitrateScale). The scale never raises a rate past the recorder's 160 Mbps:
// for a better picture than the standard the dialog records at a constant quality instead, which
// has no rate to run out of. PushRecord.js repeats this rule for the dialog's estimate: change
// both together. Pure: builds anywhere.
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace veyra::engine {
inline constexpr uint32_t kPushRecordScaleMin = 25, kPushRecordScaleMax = 200;
inline constexpr uint32_t kPushRecordMaxMbps = 160;
inline uint32_t pushRecordAutoMbps(uint32_t width, uint32_t height, uint32_t fps, bool hevc, uint32_t scalePercent = 100) {
    const double rate = std::min(fps, 60u) + std::max(int(fps) - 60, 0) * 0.5;
    const double mbps = double(width) * height * rate * 0.3 * (hevc ? 0.65 : 1.0) / 1e6;
    const long standard = std::clamp<long>(std::lround(mbps), hevc ? 8 : 10, long(kPushRecordMaxMbps));
    const uint32_t scale = std::clamp(scalePercent, kPushRecordScaleMin, kPushRecordScaleMax);
    if (scale == 100) return uint32_t(standard);
    return uint32_t(std::clamp<long>(std::lround(double(standard) * scale / 100.0), hevc ? 4 : 5, long(kPushRecordMaxMbps)));
}
} // namespace veyra::engine
