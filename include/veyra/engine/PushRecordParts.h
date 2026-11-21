// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// A recording in parts, cut without a gap.
//
// Until now a new part was a new recording: the encoder was closed and opened again, and whatever
// was presented while that took was not recorded, nor the sound between the two files. Here one
// encoder keeps running and only the file changes, as the engine release PushFPS is built on does
// it: a part ends at a keyframe, the next part starts with that keyframe, and every picture and
// every sound packet lands in exactly one part.
//
// This header is the whole decision, with no file and no codec in it, so it is tested anywhere
// (tests/unit/PushRecordPartsTests.cpp, also g++). PushPartWriter.cpp does what it says.
//
// Two things are PushFPS's own:
//   - By size, a part ends at the keyframe BEFORE the size would be passed, not after. A size is
//     chosen because something refuses a larger file (FAT32: 4 GB), so "about a group of pictures
//     over" is the one outcome that must not happen. What the next group may take is judged from
//     the largest one so far, with a quarter added (reserveFor).
//   - Sound is written up to the picture before a part is closed, and a packet that arrives for a
//     part that is already closed is counted, never written into the wrong file.
//
// What stays as it is: a part can only start at a keyframe, so by time it ends at the first
// keyframe from the chosen length on (at most one keyframe interval later). A sound packet is
// 1024 samples and is never cut in two: the one that lies across the cut belongs to the older
// part, so a part's sound can begin up to 21 ms after its picture.
#pragma once

#include <algorithm>
#include <cstdint>

namespace veyra::engine::recordparts {

struct Limits {
    int64_t afterUs = 0;      // a part ends at the first keyframe from this long on (0: not by time)
    uint64_t afterBytes = 0;  // a part ends at a keyframe before it would pass this size (0: not by size)
    constexpr bool any() const { return afterUs > 0 || afterBytes > 0; }
};

// One part, as its writer knows it.
struct Part {
    uint64_t frames = 0;   // pictures written into it
    int64_t originUs = 0;  // the recording's time of its first picture; its own times start at 0
    uint64_t bytes = 0;    // pictures and sound given to it so far
    bool open = false;
};

// What the next group of pictures may take, from the largest one so far.
constexpr uint64_t reserveFor(uint64_t largestGroupBytes) { return largestGroupBytes + largestGroupBytes / 4; }

// The recording's time in microseconds as a count of sound samples, rounded to the nearest.
constexpr int64_t samplesAt(int64_t us, int64_t rate) {
    return us >= 0 ? (us * rate + 500000) / 1000000 : -((-us * rate + 500000) / 1000000);
}

enum class SoundGoes { Current, Previous, Lost };

class Plan {
public:
    constexpr Plan() = default;
    constexpr Plan(Limits limits, uint32_t fps, int64_t audioRate)
        : limits_(limits), fps_(fps ? fps : 1), rate_(audioRate > 0 ? audioRate : 48000) { current_.open = true; }

    constexpr const Limits& limits() const { return limits_; }
    constexpr const Part& current() const { return current_; }
    constexpr const Part& previous() const { return previous_; }
    constexpr unsigned parts() const { return parts_; }                 // 1: one file so far
    constexpr uint64_t largestGroup() const { return std::max(largest_, group_); }
    constexpr uint64_t lostSound() const { return lostSound_; }
    // The file's own size, when the writer knows it is further than the packets add up to.
    constexpr void fileSize(uint64_t bytes) { position_ = bytes; }
    constexpr uint64_t bytesNow() const { return std::max(current_.bytes, position_); }

    // The picture that follows what was written: does the part end before it?
    constexpr bool endsBefore(bool key, int64_t ptsUs) const {
        if (!key || !current_.frames || !limits_.any()) return false;
        // A keyframe due on the boundary can come a fraction of a frame early on the recording's
        // clock: it is taken, so a whole keyframe interval is not added for it.
        const int64_t halfFrame = 500000 / int64_t(fps_);
        if (limits_.afterUs > 0 && ptsUs - current_.originUs >= limits_.afterUs - halfFrame) return true;
        if (limits_.afterBytes > 0 && bytesNow() + reserveFor(largestGroup()) > limits_.afterBytes) return true;
        return false;
    }
    // The writer closed the part for pictures and opened the next, which starts with the picture at ptsUs.
    // The older part stays open for sound that is still on its way, until closePrevious().
    constexpr void startNext(int64_t ptsUs) {
        previous_ = current_;
        current_ = Part{};
        current_.originUs = ptsUs;
        current_.open = true;
        largest_ = std::max(largest_, group_);
        group_ = 0;
        position_ = 0;
        ++parts_;
    }
    constexpr void closePrevious() { previous_.open = false; }

    constexpr void wroteVideo(uint64_t bytes, bool key) {
        if (key) { largest_ = std::max(largest_, group_); group_ = 0; }
        group_ += bytes;
        current_.bytes += bytes;
        ++current_.frames;
    }
    // A sound packet that starts at ptsSamples of the recording's clock.
    constexpr SoundGoes soundGoes(int64_t ptsSamples) const {
        const bool older = previous_.open && ptsSamples < samplesAt(current_.originUs, rate_);
        const Part& part = older ? previous_ : current_;
        // Before the first part's start is the encoder's own lead-in: it belongs to that part.
        if (part.originUs && ptsSamples < samplesAt(part.originUs, rate_)) return SoundGoes::Lost;
        return older ? SoundGoes::Previous : SoundGoes::Current;
    }
    // Where a packet's time starts in its part's own time (samples).
    constexpr int64_t originSamples(SoundGoes where) const {
        return samplesAt(where == SoundGoes::Previous ? previous_.originUs : current_.originUs, rate_);
    }
    constexpr void wroteSound(uint64_t bytes, SoundGoes where) {
        if (where == SoundGoes::Current) { current_.bytes += bytes; group_ += bytes; }
        else if (where == SoundGoes::Previous) previous_.bytes += bytes;
        else ++lostSound_;
    }

private:
    Limits limits_{};
    uint32_t fps_ = 60;
    int64_t rate_ = 48000;
    Part current_{}, previous_{};
    unsigned parts_ = 1;
    uint64_t group_ = 0, largest_ = 0, position_ = 0, lostSound_ = 0;
};

}  // namespace veyra::engine::recordparts
