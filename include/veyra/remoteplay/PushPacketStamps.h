// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PS5 Remote Play decoding: the stamps that tie a decoded picture back to the packet it came from, and when a
// failed picture costs the graphics card's decoder.
//
// Why (4.5.4; reports of PS5 streams that stop and then end in error 2004): every packet given to FFmpeg left a
// stamp, and only a picture that came out took its stamp away. A packet the decoder took and never answered with
// a picture (a frame lost on the network, a picture it threw away) left its stamp for good. After 64 of them every
// packet was refused as a decode failure, and that failure switched the session from the graphics card to the
// processor for good: a long session with some packet loss ended up decoding a 60 FPS stream on the processor,
// fell behind and could stall. The same switch was made for one damaged picture, which the processor cannot
// decode any better than the card.
//   - A PS5 stream is decoded in the order it is sent (no reordered pictures): once a picture comes out, every
//     older packet that is still waiting will never come out. `prune` takes those stamps away. With reordering
//     (has_b_frames above 0) nothing is pruned, as before.
//   - A full list makes room by dropping its oldest stamp instead of failing the packet.
//   - `HardwareStrikes`: the card's decoder is given up only after it failed twice with no picture decoded in
//     between; one damaged picture waits for the next keyframe on the card.
// Pure; tested with g++ (tests/unit/PushPacketStampsTests.cpp).
#pragma once
#include <cstddef>
#include <cstdint>

namespace veyra::remoteplay::stamps {

constexpr std::size_t kMaxStamps = 64;

// Removes every stamp older than `id` (by id) when the stream is decoded in order. Returns how many were removed.
template <class Deque>
std::size_t prune(Deque& stamps, std::int64_t id, int reorderDepth) {
    if (reorderDepth > 0) return 0;
    std::size_t removed = 0;
    while (!stamps.empty() && stamps.front().id < id) { stamps.pop_front(); ++removed; }
    return removed;
}

// Makes room for one more stamp. Returns how many were dropped (0 or more).
template <class Deque>
std::size_t makeRoom(Deque& stamps, std::size_t limit = kMaxStamps) {
    std::size_t dropped = 0;
    while (!stamps.empty() && stamps.size() >= limit) { stamps.pop_front(); ++dropped; }
    return dropped;
}

struct HardwareStrikes {
    int strikes = 0;
    // A picture came out of the card's decoder.
    void decoded() noexcept { strikes = 0; }
    // The card's decoder failed. True: give it up now (switch to the processor).
    bool failed() noexcept { return ++strikes >= 2; }
    void reset() noexcept { strikes = 0; }
};

}  // namespace veyra::remoteplay::stamps
