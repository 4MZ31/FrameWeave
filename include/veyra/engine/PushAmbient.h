#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS ambient fill: what the window shows beside the picture.
//
// A console sends a 16:9 picture. On a wider screen (21:9, 32:9) Fit leaves black bars, Fill and crop
// cuts the top and bottom (and any HUD there) and Stretch distorts. Ambient fill keeps the whole
// picture, undistorted, and puts a soft, dimmed copy of the same picture behind it, scaled until it
// covers the window: the bars become a quiet extension of what is on screen.
//
// How (VideoPresenter::present, only while the switch is on and the picture leaves bars):
//   1. pipeline::PushAmbientPass averages the frame down to 96 x 54 and blurs that (two small compute
//      dispatches per presented frame);
//   2. the present shader draws that small picture over the whole window, dimmed;
//   3. the picture's own draw then leaves the bars as they are instead of painting them black.
// Recordings, streams and comparison clips are made from the frames, not from the window, so they
// never contain it. Off (the default) the present path is exactly what it was.
//
// The switch is process-wide (preference pushAmbientFill, set by the bridge): the view record the
// engine keeps is reset at every source change, and a setting must not go with it.
#include <algorithm>
#include <atomic>
#include <utility>

namespace veyra::engine {
namespace push_ambient_detail { inline std::atomic<bool> fill{false}; }
inline void setPushAmbientFill(bool on) { push_ambient_detail::fill.store(on, std::memory_order_relaxed); }
inline bool pushAmbientFill() { return push_ambient_detail::fill.load(std::memory_order_relaxed); }

// The picture, drawn renderW x renderH, leaves part of a clientW x clientH window uncovered.
inline bool pushAmbientBars(float clientW, float clientH, float renderW, float renderH) {
    if (!(clientW >= 1.0f && clientH >= 1.0f && renderW >= 1.0f && renderH >= 1.0f)) return false;
    return renderW < clientW - 1.5f || renderH < clientH - 1.5f;
}
// The size at which the same picture covers the whole window (its shape kept).
inline std::pair<float, float> pushAmbientCover(float clientW, float clientH, float renderW, float renderH) {
    if (!(clientW >= 1.0f && clientH >= 1.0f && renderW >= 1.0f && renderH >= 1.0f)) return {0.0f, 0.0f};
    const float scale = std::max(clientW / renderW, clientH / renderH);
    return {renderW * scale, renderH * scale};
}
// How far the bars are dimmed. In linear light where the output is linear or can be decoded (HDR);
// on display-encoded SDR values otherwise, where 0.6 is about the same step for the eye.
inline constexpr float kPushAmbientLinearGain = 0.30f;
inline constexpr float kPushAmbientEncodedGain = 0.60f;
}
