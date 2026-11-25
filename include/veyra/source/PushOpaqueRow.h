// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#pragma once
// Frame Input API: alpha is ignored, as the API says. A swap chain's back buffer often
// carries alpha 0, and a picture passed on with that alpha showed black (issue 24), so a
// row of BGRA8 / RGBA8 pixels is copied with every alpha byte set to 255. Both formats keep
// alpha in the fourth byte, so one rule serves both. Pure; no Windows headers.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace veyra::source {

inline void pushCopyOpaqueRow(uint8_t* to, const uint8_t* from, uint32_t pixels) {
    for (uint32_t x = 0; x < pixels; ++x) {
        uint32_t v;
        std::memcpy(&v, from + size_t(x) * 4u, 4);
        const uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), 255};
        std::memcpy(to + size_t(x) * 4u, b, 4);
    }
}

} // namespace veyra::source
