// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS extra audio: the platform-free input buffer (engine/PushAuxAudio.h).
#include "veyra/engine/PushAuxAudio.h"

#include <algorithm>
#include <cmath>

namespace veyra::engine {

void PushAuxFifo::push(const float* stereo, size_t frames) {
    if (!stereo || !frames) return;
    for (size_t i = 0; i < frames * 2; ++i) data_.push_back(std::isfinite(stereo[i]) ? stereo[i] : 0.0f);
    if (data_.size() > limit_ * 2) {
        const size_t extra = data_.size() - limit_ * 2;
        data_.erase(data_.begin(), data_.begin() + std::ptrdiff_t(extra));
        dropped_ += extra / 2;
    }
    if (!primed_ && data_.size() >= prebuffer_ * 2) primed_ = true;
}

bool PushAuxFifo::mixInto(float* out, size_t frames, float gain) {
    if (!out || !frames) return false;
    if (!primed_) return false;
    if (data_.size() < frames * 2) {
        // Dry: what is left plays, then silence until the prebuffer is full again.
        const size_t have = data_.size();
        for (size_t i = 0; i < have; ++i) out[i] += data_[i] * gain;
        data_.clear();
        primed_ = false;
        ++underruns_;
        return false;
    }
    for (size_t i = 0; i < frames * 2; ++i) out[i] += data_[i] * gain;
    data_.erase(data_.begin(), data_.begin() + std::ptrdiff_t(frames * 2));
    return true;
}

} // namespace veyra::engine
