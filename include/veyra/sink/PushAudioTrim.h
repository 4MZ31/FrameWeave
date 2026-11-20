// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Lip sync by hand, on top of the automatic sound delay (PushFPS 4.0).
//
// Automatic sync delays the sound by as long as the picture takes inside PushFPS, measured from the
// moment the capture card hands each over. Two delays it cannot see: how much later than its sound
// the card hands over its picture (every card differs), and what comes after PushFPS (the screen's
// own delay on one side; a soundbar, a television's speakers or wireless headphones on the other).
// So the mouth and the voice can still be apart, either way, and only the person watching can tell.
// The trim is that person's correction: milliseconds added to the automatic delay. Positive plays
// the sound later, negative earlier. Sound cannot be played before it arrives, so a negative trim
// ends where the automatic delay is used up; the session reports that as "limited".
// It is one number for the whole process (the sound device and the screen do not change with the
// game), kept in preference pushAudioTrimMs, and it acts in automatic mode only: the manual mode
// already is a number set by hand, and "off" means no delay at all.
#pragma once
#include <algorithm>
#include <atomic>
namespace veyra::sink {
inline constexpr int kPushAudioTrimLimitMs = 250;
inline std::atomic<int>& pushAudioTrimStore() { static std::atomic<int> value{0}; return value; }
inline void setPushAudioTrimMs(int ms) { pushAudioTrimStore().store(std::clamp(ms, -kPushAudioTrimLimitMs, kPushAudioTrimLimitMs), std::memory_order_relaxed); }
inline int pushAudioTrimMs() { return pushAudioTrimStore().load(std::memory_order_relaxed); }
// The delay asked of the sound, before it is held inside what is possible (0 .. the mode's maximum).
// mode: 0 automatic (the picture's delay plus the trim), 1 manual (the offset alone), 2 off.
constexpr double pushAudioRequestedMs(unsigned mode, double automaticMs, int manualOffsetMs, int trimMs) {
    return mode == 0 ? automaticMs + double(trimMs) : mode == 1 ? double(manualOffsetMs) : 0.0;
}
} // namespace veyra::sink
