#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS streaming mode extra audio (qml/Veyra/StudioPage.qml mixer strips, bridge PushStudio.cpp):
//  - Desktop audio: everything Windows plays except PushFPS itself (Discord, music, browser), through
//    WASAPI process loopback with PushFPS's process tree excluded, so the game PushFPS plays is never
//    taken twice.
//  - Extra input: a second microphone, a line-in or an audio interface channel.
// Each has a fader (-60..+12 dB) and mute. One thread captures both (shared mode, converted by
// Windows to 48 kHz float stereo), mixes them in 10 ms blocks stamped with the recording clock and
// hands the blocks to every recorder and stream (LiveRecorder's aux lane, mixed into the sound
// track). Per-input meters are fed after the fader. It runs while at least one recorder listens or
// the streaming mode's meters are on.
#include "veyra/engine/PushAudioMix.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace veyra::engine {

struct PushAuxSettings {
    bool desktop = false; float desktopDb = 0; bool desktopMuted = false;
    bool input = false; std::wstring inputId; float inputDb = 0; bool inputMuted = false;
    bool operator==(const PushAuxSettings&) const = default;
};

class PushAuxSink {
public:
    virtual ~PushAuxSink() = default;
    // Aux thread: copy and return, never block. 48 kHz interleaved float stereo.
    virtual void auxPcm(const float* stereo, size_t frames, int64_t capturedUs) = 0;
};

struct PushAuxStatus {
    bool desktopOpen = false, inputOpen = false;
    std::wstring desktopError, inputError, inputName;
};

void setPushAux(const PushAuxSettings& settings);
PushAuxSettings pushAux();
bool pushAuxEnabled();                  // desktop audio or the extra input is on
void attachPushAux(std::shared_ptr<PushAuxSink> sink);
void detachPushAux(const PushAuxSink* sink);
void setPushAuxMeters(bool on);         // capture for the meters with nothing recording
PushAuxStatus pushAuxStatus();
PushLevelMeter& pushDesktopMeter();
PushLevelMeter& pushInputMeter();

// One input's buffer between its device and the 10 ms mix (platform free, unit-tested): it starts
// giving samples once `prebuffer` frames are in, keeps at most `limit` frames (the oldest go, so a
// faster device never builds delay), and an input that runs dry gives silence and refills.
class PushAuxFifo {
public:
    explicit PushAuxFifo(size_t prebuffer = 960, size_t limit = 4800) : prebuffer_(prebuffer), limit_(limit) {}
    void push(const float* stereo, size_t frames);
    // Adds `frames` frames times gain into out (stereo); false when it gave silence.
    bool mixInto(float* out, size_t frames, float gain);
    size_t frames() const { return data_.size() / 2; }
    void clear() { data_.clear(); primed_ = false; }
    uint64_t underruns() const { return underruns_; }
    uint64_t dropped() const { return dropped_; }
private:
    std::deque<float> data_;
    size_t prebuffer_, limit_;
    bool primed_ = false;
    uint64_t underruns_ = 0, dropped_ = 0;
};

} // namespace veyra::engine
