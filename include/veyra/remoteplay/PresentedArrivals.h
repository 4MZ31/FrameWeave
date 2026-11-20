// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#pragma once
// PS5 Remote Play: when did the frame that was just shown arrive? (PushFPS 4.1; users reported the
// sound about two seconds behind the picture in Remote Play.)
//
// Automatic sound sync delays the sound by the time the picture takes. It works that out from the
// shown frame's time stamp, and for every other source it also gets the moment that frame arrived, so
// a time stamp that does not fit what really happened is not believed (sink/CaptureSyncTarget.h: more
// than 80 ms apart and the real arrival-to-screen time is used). Remote Play gave it the time stamp
// alone. Its two time lines are estimates made here, not the console's: the picture's follows its
// arrivals, the sound's is its first block's arrival plus the samples counted since. If that first
// block came late (a session's start, a reconnect) every later sound was stamped that much too late,
// the sync took the difference for picture delay, and the sound was held back by it, up to the
// 1.5 second limit: with the output's own buffer, about two seconds.
//
// This is the missing piece: the arrival of each frame handed to the player, found again by its time
// stamp when it is shown. Pure: no locks (the owner locks), no clock.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace veyra::remoteplay {

class PresentedArrivals {
public:
    static constexpr std::size_t kKept = 64;          // over a second of frames at 60 a second
    static constexpr std::int64_t kTolerance = 5;     // 100 ns units: the same stamp after a round trip through milliseconds

    void clear() { count_ = next_ = 0; }
    // A frame handed to the player: its time stamp and when it arrived, both in 100 ns.
    void note(std::int64_t pts100ns, std::int64_t arrival100ns) {
        if (arrival100ns <= 0) return;
        entries_[next_] = {pts100ns, arrival100ns};
        next_ = (next_ + 1) % kKept;
        if (count_ < kKept) ++count_;
    }
    // The arrival of the frame with this time stamp, the newest first; nothing for a stamp that was
    // never handed over (a generated frame's, or one from before the list's memory).
    std::optional<std::int64_t> find(std::int64_t pts100ns) const {
        for (std::size_t i = 0; i < count_; ++i) {
            const auto& e = entries_[(next_ + kKept - 1 - i) % kKept];
            const std::int64_t d = e.pts - pts100ns;
            if (d >= -kTolerance && d <= kTolerance) return e.arrival;
        }
        return std::nullopt;
    }
    std::size_t size() const { return count_; }

private:
    struct Entry { std::int64_t pts = 0, arrival = 0; };
    std::array<Entry, kKept> entries_{};
    std::size_t count_ = 0, next_ = 0;
};

}  // namespace veyra::remoteplay
