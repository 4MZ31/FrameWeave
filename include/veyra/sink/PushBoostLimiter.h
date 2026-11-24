// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Volume above 100 %: a loud peak is turned down, not cut off.
//
// Why (reports of crackling sound, November 2026): the volume goes up to 300 %, also with the arrow keys,
// and everything past full scale was then clamped sample by sample (AudioGain.h, limitPcmPeaks). A game's
// loud moments come out with flat tops, which is heard as crackling and distortion. Here the whole signal
// is turned down for as long as a peak needs it and comes back over about 80 ms, the way the sound
// profiles' own limiter works (SoundProfile.h): the waveform keeps its shape and nothing passes the ceiling.
//
// Up to 100 % none of this runs: the samples are what they always were. The one exception is the fraction of
// a second after the volume came back down from above 100 %, while the turned-down signal glides back to
// full volume instead of jumping there.
// Pure: no platform header, so the maths is tested without a sound device (veyra_push_boost_limiter_tests).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace veyra::sink {

struct PushBoostLimiter {
    static constexpr float kCeiling = 0.98f;              // just under full scale
    // Back towards full volume with a time constant of 80 ms at the renderer's 48 kHz: 1 - exp(-1 / 3840).
    static constexpr float kRelease = 0.00026038f;
    // Close enough to full volume to be there (0.009 dB). Not closer: a float stops gaining a little above
    // 0.9998, where one release step is smaller than the distance to the next number.
    static constexpr float kBack = 0.999f;

    float gain = 1.0f;                                    // what the signal is multiplied by right now, 0..1

    void reset() noexcept { gain = 1.0f; }

    // The volume is at 100 % or less. Nothing is touched, unless the signal is still turned down from a
    // boost that just ended: then it comes back to full volume over the same 80 ms, without a step.
    void finish(float* samples, std::size_t frames, unsigned channels) noexcept {
        if (gain >= kBack) { gain = 1.0f; return; }
        if (!samples || !channels) return;
        for (std::size_t i = 0; i < frames && gain < kBack; ++i) {
            gain += (1.0f - gain) * kRelease;
            for (unsigned c = 0; c < channels; ++c) samples[i * channels + c] *= gain;
        }
    }

    // Interleaved float samples, in place. Every channel of a frame gets the same gain, so the picture of
    // the sound between the speakers does not move. A sample that is not a number becomes silence.
    void process(float* samples, std::size_t frames, unsigned channels) noexcept {
        if (!samples || !channels) return;
        for (std::size_t i = 0; i < frames; ++i) {
            float* frame = samples + i * channels;
            float peak = 0.0f;
            for (unsigned c = 0; c < channels; ++c) {
                if (!std::isfinite(frame[c])) frame[c] = 0.0f;
                peak = std::max(peak, std::abs(frame[c]));
            }
            const float wanted = peak > kCeiling ? kCeiling / peak : 1.0f;
            // Down at once, so nothing passes the ceiling; up slowly, and never further than this frame allows.
            gain = wanted < gain ? wanted : std::min(wanted, gain + (1.0f - gain) * kRelease);
            for (unsigned c = 0; c < channels; ++c) frame[c] *= gain;
        }
    }
};

} // namespace veyra::sink
