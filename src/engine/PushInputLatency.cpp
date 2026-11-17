// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS automatic Remote Play latency test: change detection and summary (pure).
#include "veyra/engine/PushInputLatency.h"

#include <algorithm>
#include <cmath>

namespace veyra::engine::latency {

double regionDiff(const quality::Luma& a, const quality::Luma& b, float top) {
    if (!a.valid() || !b.valid()) return 0;
    const uint32_t y0 = std::min(quality::kGridH - 1, uint32_t(std::clamp(top, 0.0f, 1.0f) * float(quality::kGridH)));
    double sum = 0; size_t n = 0;
    for (uint32_t y = y0; y < quality::kGridH; ++y)
        for (uint32_t x = 0; x < quality::kGridW; ++x) {
            const size_t i = size_t(y) * quality::kGridW + x;
            sum += std::abs(double(a.y[i]) - double(b.y[i])); ++n;
        }
    return n ? sum / double(n) : 0;
}

void Detector::reset() { base_ = {}; previous_ = {}; noise_ = 0; samples_ = 0; }
void Detector::before(const quality::Luma& frame) {
    if (!frame.valid()) return;
    if (previous_.valid()) noise_ = std::max(noise_ * 0.8, regionDiff(previous_, frame));   // decaying peak of normal motion
    previous_ = frame; base_ = frame; ++samples_;
}
double Detector::threshold() const { return std::max(0.04, noise_ * 3.0); }
bool Detector::answered(const quality::Luma& frame) const {
    return ready() && frame.valid() && regionDiff(base_, frame) > threshold();
}

Summary summarize(const std::vector<double>& ms, uint32_t trials) {
    Summary s;
    s.trials = trials; s.answered = uint32_t(ms.size());
    if (ms.empty()) return s;
    std::vector<double> v = ms;
    std::sort(v.begin(), v.end());
    double sum = 0; for (double x : v) sum += x;
    s.meanMs = sum / double(v.size());
    s.medianMs = v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
    s.minMs = v.front(); s.maxMs = v.back();
    return s;
}

} // namespace veyra::engine::latency
