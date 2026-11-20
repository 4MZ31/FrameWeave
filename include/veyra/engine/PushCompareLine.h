// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The line between the two sides of the live split comparison ("compare to original"): where it
// is and how thick, in back-buffer pixels. Pure, so it is tested without a graphics card; the
// presenter draws it (VideoPresenter.cpp).
#pragma once
#include <algorithm>
namespace veyra::engine {
struct PushCompareLine {
    bool visible = false;
    long outerLeft = 0, outerRight = 0;   // the dark edges' outside
    long coreLeft = 0, coreRight = 0;     // the white core
};
// Reference white on an HDR output: 203 nits as a PQ code value, and in scRGB (1.0 = 80 nits).
inline constexpr float kPushCompareWhitePq = 0.5807f;
inline constexpr float kPushCompareWhiteScRgb = 203.0f / 80.0f;
// The white core's width for a buffer this high: 3 pixels at 1080, 4 at 1440, 6 at 2160, never under 2.
constexpr long pushCompareLineCore(long bufferHeight) { return std::max(2L, (bufferHeight + 180) / 360); }
// One dark edge's width: 1 pixel up to 1080 high, 2 at 1440, 3 at 2160.
constexpr long pushCompareLineEdge(long bufferHeight) { return std::max(1L, pushCompareLineCore(bufferHeight) / 2); }
// x: where the original's side ends. pictureLeft / pictureRight: the picture's own edges.
// The line is centred on x and never leaves the picture. With the split at the picture's very edge
// (one side has nothing to show) there is no line: there is nothing to tell apart.
constexpr PushCompareLine pushCompareLine(long x, long pictureLeft, long pictureRight, long bufferHeight) {
    PushCompareLine line;
    if (pictureRight <= pictureLeft || x <= pictureLeft || x >= pictureRight) return line;
    const long core = pushCompareLineCore(bufferHeight), edge = pushCompareLineEdge(bufferHeight);
    const long coreLeft = x - core / 2, coreRight = coreLeft + core;
    line.coreLeft = std::clamp(coreLeft, pictureLeft, pictureRight);
    line.coreRight = std::clamp(coreRight, pictureLeft, pictureRight);
    line.outerLeft = std::clamp(coreLeft - edge, pictureLeft, pictureRight);
    line.outerRight = std::clamp(coreRight + edge, pictureLeft, pictureRight);
    line.visible = line.coreRight > line.coreLeft;
    return line;
}
} // namespace veyra::engine
