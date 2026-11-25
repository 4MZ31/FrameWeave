// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Reading write-combined memory (a D3D12 upload buffer mapped for the CPU, CaptureUploadFrame.h) on the CPU.
// Ordinary loads from it are uncached, one round trip each, so code that reads it byte by byte runs at a
// fraction of normal speed (4.5.3: the quality test did, and the output fell to about 4 FPS). Streaming
// loads (SSE4.1, which every CPU Windows 11 runs on has) take 64 bytes at a time. Pure: tested with g++.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#if defined(_MSC_VER) || defined(__SSE4_1__)
#include <smmintrin.h>
#define PUSHFPS_STREAM_LOADS 1
#endif

namespace veyra::engine {

// Copies `bytes` from `from` (write-combined or not; any alignment) to ordinary memory at `to`.
inline void copyFromWriteCombined(uint8_t* to, const uint8_t* from, size_t bytes) {
    size_t i = 0;
#ifdef PUSHFPS_STREAM_LOADS
    // Streaming loads need 16-byte alignment; a mapped upload buffer starts on 64 KiB and its rows on 256.
    while (i < bytes && (reinterpret_cast<uintptr_t>(from + i) & 15) != 0) { to[i] = from[i]; ++i; }
    for (; i + 64 <= bytes; i += 64) {
        auto* p = reinterpret_cast<__m128i*>(const_cast<uint8_t*>(from + i));
        const __m128i a = _mm_stream_load_si128(p), b = _mm_stream_load_si128(p + 1);
        const __m128i c = _mm_stream_load_si128(p + 2), d = _mm_stream_load_si128(p + 3);
        auto* q = reinterpret_cast<__m128i*>(to + i);
        _mm_storeu_si128(q, a); _mm_storeu_si128(q + 1, b); _mm_storeu_si128(q + 2, c); _mm_storeu_si128(q + 3, d);
    }
#endif
    if (i < bytes) std::memcpy(to + i, from + i, bytes - i);
}

} // namespace veyra::engine
