// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The PS5 pairing fields as people fill them in, which controller is read, and the no-controller notice.
// Builds anywhere: g++ -std=c++20 -Iinclude tests/unit/PushPairingInputTests.cpp src/remoteplay/Validation.cpp
#include "veyra/remoteplay/PushPadChoice.h"
#include "veyra/remoteplay/PushPairingInput.h"

#include <cstdio>
#include <string>

using namespace veyra::remoteplay;
using pairing::Problem;

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

int main() {
    const std::string host = "192.168.1.20";
    const std::string number = "1234567890123456789";
    const std::string code = accountIdToBase64(*accountIdFromDecimal(number));

    // --- the pairing number -----------------------------------------------------------------------
    check(pairing::cleanPin("1234 5678") == "12345678", "the number as the PS5 shows it, with its space");
    check(pairing::cleanPin(" 1234-5678\r\n") == "12345678", "a dash between the groups and a pasted line end");
    check(pairing::check(host, number, "1234 5678").problem == Problem::None, "pairing takes the number with the space");
    check(pairing::check(host, number, "1234 5678").pin == "12345678", "and hands on eight digits");
    check(pairing::check(host, number, "12345678").problem == Problem::None, "the number without a space, as before");
    check(pairing::check(host, number, "1234567").problem == Problem::Pin, "seven digits are not the number");
    check(pairing::check(host, number, "123456781").problem == Problem::Pin, "nine digits are not the number");
    check(pairing::check(host, number, "1234 567a").problem == Problem::Pin, "a letter in it is not the number");
    check(pairing::check(host, number, "").problem == Problem::Pin, "no number");

    // --- the account ID ---------------------------------------------------------------------------
    check(code.size() == 12 && code.back() == '=', "an account ID is also 12 characters ending in =");
    check(pairing::check(host, code, "12345678").problem == Problem::None, "the 12 characters are taken");
    check(pairing::check(host, " " + number + " ", "12345678").problem == Problem::None, "blanks round a pasted number");
    check(pairing::check(host, "1234 5678 9012 3456 789", "12345678").problem == Problem::None, "blanks inside a pasted number");
    check(pairing::account(number) && pairing::account(code) && *pairing::account(number) == *pairing::account(code),
          "both forms are the same account");
    for (const char* name : {"SomePlayers", "Player_One", "abc", "some-player-16ch", "Jo3", "PlayerOne99"}) {
        const auto result = pairing::check(host, name, "12345678");
        check(result.problem == Problem::Account, std::string("a PSN name is not an account ID: ") + name);
    }
    // Eleven letters with '=' put behind them decode: that is why a name is never completed to an ID.
    check(accountIdFromBase64("SomePlayers=").has_value(), "eleven letters and a '=' would decode as someone's ID");
    check(!pairing::account("SomePlayers"), "so eleven letters alone are never taken for one");
    check(pairing::check(host, "", "12345678").problem == Problem::Account, "no account ID");
    check(pairing::check(host, code.substr(0, 11), "12345678").problem == Problem::Account, "the 12 characters without their =");
    check(pairing::check(host, "99999999999999999999999", "12345678").problem == Problem::Account, "a number too long to be one");

    // --- the address ------------------------------------------------------------------------------
    check(pairing::check("", number, "12345678").problem == Problem::Host, "no address");
    check(pairing::check("192.168.1", number, "12345678").problem == Problem::Host, "three groups are not an address");
    check(pairing::check("http://192.168.1.20", number, "12345678").problem == Problem::Host, "a web address is not one");
    check(pairing::check(" 192.168.1.20 ", number, "12345678").problem == Problem::None, "blanks round the address");
    check(pairing::check("192.168.1. 20", number, "12345678").host == "192.168.1.20", "a blank typed inside it");
    check(pairing::check("ps5.home", number, "12345678").problem == Problem::None, "a name on the network");
    // Top to bottom: the first field that is wrong is the one named.
    check(pairing::check("", "SomePlayers", "1").problem == Problem::Host, "everything wrong: the address is named first");
    check(pairing::check(host, "SomePlayers", "1").problem == Problem::Account, "then the account ID");

    // --- which controller is read -----------------------------------------------------------------
    {
        PadChoice one;
        check(one.watching(false), "before any button the other controllers are watched");
        check(!one.pressed(7, 7, false) && one.settled, "a button on the controller being read settles it");
        check(!one.watching(false), "after that nothing else is watched: as it always was");
        check(!one.pressed(9, 7, false), "a button on another controller no longer changes anything");
    }
    {
        PadChoice wrong;                                   // a wheel is listed first
        check(wrong.pressed(9, 7, false), "the first button comes from another controller: that one is read");
        check(wrong.settled && !wrong.watching(false), "and the choice is settled with it");
        check(!wrong.pressed(7, 9, false), "the first one listed does not take it back");
        check(!wrong.pressed(9, 9, false), "its own buttons change nothing");
    }
    {
        PadChoice mirrored;                                // a tool shows the same pad twice
        const bool first = mirrored.pressed(9, 7, false);  // the real one's event arrives first
        const bool second = mirrored.pressed(7, 9, false); // then the copy's
        check(first && !second, "one press seen on two controllers switches once, not back and forth");
    }
    {
        PadChoice again;
        again.pressed(7, 7, false);
        again.reset();
        check(again.watching(false) && again.pressed(9, 7, false), "after the controller disconnects the choice is open again");
    }
    {
        PadChoice follow;                                  // PS5 controller control, unchanged
        check(follow.pressed(9, 7, true) && follow.pressed(7, 9, true) && follow.pressed(9, 7, true),
              "follow mode: whoever presses is read, every time");
        check(follow.watching(true), "and every controller stays open");
        check(!follow.watching(false), "leaving follow mode: the pad read was pressed, so the others are closed as before");
    }

    // --- nobody's controller ----------------------------------------------------------------------
    {
        const HostTime second = 10'000'000;
        PadMissing m;
        check(!m.due(true, true, false, 100 * second), "not at once");
        check(!m.due(true, true, false, 105 * second), "not after five seconds");
        check(m.due(true, true, false, 106 * second), "after six seconds of playing without a controller");
        check(!m.due(true, true, false, 200 * second), "once a stream");
        check(!m.due(false, true, false, 201 * second), "nothing when nothing plays");
        check(!m.due(true, true, false, 300 * second) && m.due(true, true, false, 306 * second), "the next stream says it again");
    }
    {
        const HostTime second = 10'000'000;
        PadMissing m;
        check(!m.due(true, true, false, 10 * second) && !m.due(true, true, true, 12 * second), "a controller that turns up in time: nothing");
        check(!m.due(true, true, true, 60 * second), "nothing while it is there");
        check(!m.due(true, true, false, 61 * second) && m.due(true, true, false, 67 * second), "it went away for good: said");
        PadMissing view;
        check(!view.due(true, false, false, 10 * second) && !view.due(true, false, false, 99 * second), "view only: no controller is wanted");
        PadMissing zero;
        check(!zero.due(true, true, false, 0) && zero.due(true, true, false, PadMissing::kWait + 1), "a clock that starts at 0");
    }

    std::printf(failures ? "\nFAILED: %d\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
