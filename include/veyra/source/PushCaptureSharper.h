#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// A sharper capture format at another frame rate (after PushFPS 4.1).
//
// The capture dialog lists the pixel formats a card offers at the chosen resolution AND frame rate. Many
// USB HDMI cards offer an uncompressed format (YUY2, NV12) at 1080p only up to 30 frames a second and
// MJPEG at 60. A resolution starts at its first common rate, usually 60, so the pixel format menu then
// holds MJPEG alone, and people concluded that PushFPS cannot use YUY2 (a user's message of 20 November
// 2026: "in PushFPS I can only seem to use MJPEG", with the same card on YUY2 at 1080p30 in OBS).
// MJPEG is compressed in the card: the picture is softer and carries block artifacts before PushFPS has
// done anything, and it costs a decode.
//
// So when everything offered at the chosen rate is compressed, and the same resolution has an
// uncompressed format at another common rate of at least kPushSharperMinFps, the dialog says so and
// offers it in one click. Nothing is ever switched by itself: at 30 the card hands over half the frames
// of a 60 FPS game, which only the user can weigh.
// Pure: no device is asked here.
#include "veyra/source/CaptureFormatSelection.h"
#include <cmath>
#include <span>
#include <string_view>
namespace veyra::source {
// CaptureFormat::tier of a format that needs a decoder (CaptureFormatTier::Decoded; Capture.cpp holds the two together).
inline constexpr int kPushCompressedTier = 3;
// Below this an uncompressed format is no use for a game (a USB 2 link's YUY2 at 1080p is 5 a second).
inline constexpr double kPushSharperMinFps = 24.0;

inline bool pushCaptureCompressed(const CaptureFormat& format) {
    return format.tier >= kPushCompressedTier || captureMjpegFormat(format);
}
// The format to offer, or null: nothing chosen yet, an uncompressed format already stands in the menu,
// nothing usable is offered at the chosen rate, or no other rate has one.
// `rate` is the chosen frame rate's group id (captureFrameRateGroupId).
inline const CaptureFormat* pushSharperCaptureFormat(std::span<const CaptureFormat> formats, std::wstring_view resolution, std::wstring_view rate) {
    if (resolution.empty() || rate.empty()) return nullptr;
    bool compressedHere = false;
    for (const auto& f : formats) {
        if (f.automatic || !f.supported || captureResolutionId(f) != resolution || captureFrameRateGroupId(f) != rate) continue;
        if (!pushCaptureCompressed(f)) return nullptr;
        compressedHere = true;
    }
    if (!compressedHere) return nullptr;
    const CaptureFormat* best = nullptr;
    for (const auto& f : formats) {
        if (f.automatic || !f.supported || f.interlaced || captureResolutionId(f) != resolution) continue;
        if (pushCaptureCompressed(f) || f.fps < kPushSharperMinFps || !captureCommonFrameRate(f.fps)) continue;
        // The highest rate; at one rate the format that costs least (the list's own order of preference).
        if (!best || f.fps > best->fps + 0.01 || (std::abs(f.fps - best->fps) <= 0.01 && f.rank < best->rank)) best = &f;
    }
    return best;
}
}
