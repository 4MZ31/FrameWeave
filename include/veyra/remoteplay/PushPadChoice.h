// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Which controller is read when Windows lists more than one, and when to say that there is none.
//
// Why (reports of "it connects but my controller does nothing", November 2026): the first controller
// Windows lists was the one read, for good. With a wheel, a flight stick or a program's virtual pad
// listed first, the pad in the player's hands was never looked at, and nothing said so.
//
// The rule: the first one listed is still the one read. Until a button has been pressed on it, a button
// pressed on another controller makes that one the controller. From the first button on the controller
// being read the choice is settled, the others are closed, and everything is as it always was, until
// that controller disconnects. With one controller nothing changes at all.
// Follow mode (PS5 controller control: whoever presses last is read) is what it was.
#pragma once
#include <cstdint>
#include "veyra/remoteplay/Types.h"

namespace veyra::remoteplay {

struct PadChoice {
    bool settled = false;

    // No controller is read any more (it disconnected, or reading stopped): the next one starts open.
    void reset() noexcept { settled = false; }

    // A button went down on `which` while `active` is the controller read. True: read `which` from now.
    bool pressed(std::uint32_t which, std::uint32_t active, bool follow) noexcept {
        if (which == active) { settled = true; return false; }
        if (follow) { settled = true; return true; }   // read from now on, and a button was pressed on it
        if (settled) return false;
        settled = true;
        return true;
    }

    // Whether the controllers that are not read are kept open and new ones are looked for.
    bool watching(bool follow) const noexcept { return follow || !settled; }
};

// A stream that plays while no controller is found on this PC says so once. The dialog whose status
// line says it is closed by then. Not at once: a Bluetooth controller takes a few seconds to turn up.
struct PadMissing {
    HostTime since = 0;   // when this stream was first seen without a controller (100 ns); 0 = it was not
    bool said = false;

    static constexpr HostTime kWait = 6 * 10'000'000LL;

    // Once a tick. True once a stream: after `wait` of playing with a controller wanted and none found.
    bool due(bool playing, bool wanted, bool connected, HostTime now, HostTime wait = kWait) noexcept {
        if (!playing) { since = 0; said = false; return false; }
        if (!wanted || connected) { since = 0; return false; }
        if (said) return false;
        if (since == 0) { since = now != 0 ? now : 1; return false; }
        if (now - since < wait) return false;
        said = true;
        return true;
    }
};

} // namespace veyra::remoteplay
