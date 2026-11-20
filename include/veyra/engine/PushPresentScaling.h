#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS: how the picture is resized to the window.
//
// Until 4.1 the picture was drawn with a plain bilinear sample for every source but one (PS5 Remote
// Play with reconstructed chroma, which used the fine path). A picture smaller than the window came
// out soft, and one larger than the window was shrunk without filtering, which shimmers on fine
// detail. Since 4.1 every source takes the fine path of the present shader (PresentBlit.hlsl): an
// exact copy where the picture and the window line up pixel for pixel, a Catmull-Rom cubic held to
// the range of its four nearest pixels when it is enlarged, and the same cubic widened by the shrink
// factor when it is made smaller.
//
// A 4K picture on a 4K screen is the same pixels either way. What changes is every resized picture,
// and that is a change to how the program looks, so it is a choice: preference `pushSharpScaling`,
// on unless switched off. Off is exactly what every version before 4.1 drew. The switch is process
// wide and read at every presented frame, so it acts at once and no graph is rebuilt for it.
#include <atomic>

namespace veyra::engine {
namespace push_scaling_detail { inline std::atomic<bool> sharp{true}; }
inline void setPushSharpScaling(bool on) { push_scaling_detail::sharp.store(on, std::memory_order_relaxed); }
inline bool pushSharpScaling() { return push_scaling_detail::sharp.load(std::memory_order_relaxed); }

// Whether a frame is drawn with the fine path.
//   graphFine:  the graph allows it (every source since 4.1; a test variable can forbid it).
//   askedBefore: the one case that used it before 4.1.
// With the choice on: wherever the graph allows it. With it off: only where it always was.
constexpr bool pushFinePresentation(bool graphFine, bool askedBefore, bool sharp) {
    return graphFine && (sharp || askedBefore);
}
inline bool pushFinePresentation(bool graphFine, bool askedBefore) {
    return pushFinePresentation(graphFine, askedBefore, pushSharpScaling());
}
}
