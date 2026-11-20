// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Controller states on their way from the interface (which reads the pad) to the session's owner
// (which sends them to the console). Pure: no clock, no lock, no device; the caller holds its own
// lock and hands in its own clock.
//
// What goes wrong without care. The interface reads the pad about 125 times a second and the
// owner sends one state each time round its loop. When the owner falls behind (the decoder has a
// backlog), whole states queue up: every one of them is sent late, and when the oldest had waited
// 100 ms the queue used to answer with a neutral state, sticks centred and everything released,
// although fresh states stood right behind it. That is the stutter people felt in a held trigger.
//
// The rules here:
//  - A state is two things. Edges are what was pressed or let go: the buttons, whether each
//    trigger is in the same part of its travel, which touch contacts exist, whether input is
//    active at all. Everything else (sticks, how far a trigger is in within that part, where a
//    touch is, the motion sensors) is a level: only the newest matters.
//  - A new state with the same edges as the newest waiting one replaces it, so levels never queue.
//    The motion samples of both are kept, in order, up to the state's own sixteen.
//  - A state with different edges queues behind, so presses and releases reach the console in the
//    order they happened, each with the levels it had.
//  - A state that has waited longer than kFresh is dropped by itself. What stands behind it is
//    still sent, in order. Nothing is answered with neutral while something fresh waits.
//  - Neutral is for when there is nothing true to send: the interface stopped reporting (kFresh
//    without a state), input was switched off (focus or device lost), or more edges queued up
//    than kMaxPending, where order can no longer be trusted (the existing fail-safe, kept).
// Triggers: the console gets their travel as a number and games set their own thresholds, so a
// short full pull must not arrive as its last faint value. Their travel is cut in four parts
// (released, light, half, full) and moving from one part to another is an edge.
#pragma once
#include "veyra/remoteplay/Types.h"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <utility>
namespace veyra::remoteplay {
class ControllerQueue {
public:
    static constexpr HostTime kFresh100ns = 1000000;   // 100 ms
    static constexpr std::size_t kMaxPending = 16;
    struct Stats {
        std::uint64_t states = 0;        // handed in
        std::uint64_t merged = 0;        // replaced a waiting state with the same edges
        std::uint64_t expired = 0;       // dropped by itself after waiting too long
        std::uint64_t overflows = 0;     // more edges than kMaxPending: released, then the latest
        std::uint64_t silences = 0;      // the interface stopped reporting: released
        std::size_t highWaterPending = 0;
        HostTime highWaterAge100ns = 0;  // the longest a state that was sent had waited
    };
    // Which part of its travel a trigger is in.
    static constexpr int triggerPart(std::uint8_t travel) { return travel == 0 ? 0 : travel < 86 ? 1 : travel < 171 ? 2 : 3; }
    static bool sameEdges(const ControllerState& a, const ControllerState& b) {
        return a.inputActive == b.inputActive && a.buttons == b.buttons &&
               triggerPart(a.l2) == triggerPart(b.l2) && triggerPart(a.r2) == triggerPart(b.r2) &&
               a.touches[0].id == b.touches[0].id && a.touches[1].id == b.touches[1].id;
    }
    // False when the state made the queue overflow (the caller may log it; nothing else to do).
    bool push(ControllerState state, HostTime now) {
        stamp_ = now; ++stats_.states;
        // Input switched off cancels what was queued: nothing waits behind a release.
        if (!state.inputActive) pending_.clear();
        else if (!pending_.empty() && sameEdges(pending_.back().second, state)) {
            keepMotion(pending_.back().second, state);
            pending_.back() = {now, std::move(state)};
            ++stats_.merged;
            return true;
        }
        bool fits = true;
        if (pending_.size() >= kMaxPending) {
            pending_.clear(); pending_.push_back({now, ControllerState{}});
            ++stats_.overflows; fits = false;
        }
        pending_.push_back({now, std::move(state)});
        stats_.highWaterPending = std::max(stats_.highWaterPending, pending_.size());
        return fits;
    }
    // The state to send now. With nothing waiting, the one sent last (the console keeps what it has).
    ControllerState take(HostTime now) {
        if (now - stamp_ > kFresh100ns) {
            if (!pending_.empty() || !(last_ == ControllerState{})) ++stats_.silences;
            pending_.clear(); last_ = {}; return last_;
        }
        while (!pending_.empty() && now - pending_.front().first > kFresh100ns) { pending_.pop_front(); ++stats_.expired; }
        if (!pending_.empty()) {
            if (now > pending_.front().first) stats_.highWaterAge100ns = std::max(stats_.highWaterAge100ns, now - pending_.front().first);
            last_ = std::move(pending_.front().second); pending_.pop_front();
        }
        return last_;
    }
    // A session begins or ends: nothing waits, and the state last sent is neutral.
    void clear() { pending_.clear(); last_ = {}; }
    void resetStats() { stats_ = {}; }
    std::size_t pending() const { return pending_.size(); }
    const Stats& stats() const { return stats_; }
private:
    // `next` replaces `previous`: its motion samples go behind those of `previous`, oldest first,
    // each newer than the one before, the newest sixteen kept.
    static void keepMotion(const ControllerState& previous, ControllerState& next) {
        if (!previous.motionValid || !next.motionValid) return;
        decltype(next.motionSamples) samples{}; std::size_t count = 0;
        const auto add = [&](const ControllerState::MotionSample& s) {
            if (!s.timestampUs || (count && s.timestampUs <= samples[count - 1].timestampUs)) return;
            if (count == samples.size()) { std::move(samples.begin() + 1, samples.end(), samples.begin()); --count; }
            samples[count++] = s;
        };
        for (std::size_t i = 0; i < std::min<std::size_t>(previous.motionSampleCount, previous.motionSamples.size()); ++i) add(previous.motionSamples[i]);
        for (std::size_t i = 0; i < std::min<std::size_t>(next.motionSampleCount, next.motionSamples.size()); ++i) add(next.motionSamples[i]);
        next.motionSamples = samples; next.motionSampleCount = std::uint8_t(count);
    }
    std::deque<std::pair<HostTime, ControllerState>> pending_;
    ControllerState last_{};
    HostTime stamp_ = 0;
    Stats stats_{};
};
} // namespace veyra::remoteplay
