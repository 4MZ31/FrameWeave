// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#include "veyra/remoteplay/PushPacketStamps.h"
#include <cstdio>
#include <deque>

using namespace veyra::remoteplay::stamps;
struct Stamp { std::int64_t id; int payload; };
static int failures = 0;
static void check(bool ok, const char* label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label); if (!ok) ++failures; }

int main() {
    {   // packets 0..9 given, the decoder answers only 3, 7 and 9: nothing is left waiting
        std::deque<Stamp> s; for (int i = 0; i < 10; ++i) s.push_back({i, i});
        for (std::int64_t out : {3, 7, 9}) {
            prune(s, out, 0);
            check(!s.empty() && s.front().id == out, "the answered packet's stamp is still found after pruning");
            s.pop_front();   // as drainDecoder takes it
        }
        check(s.empty(), "an in-order stream leaves no stamp behind for packets that never came out");
    }
    {   // a reordered stream (B-frames) is never pruned
        std::deque<Stamp> s; for (int i = 0; i < 4; ++i) s.push_back({i, i});
        check(prune(s, 2, 1) == 0 && s.size() == 4, "a stream with reordering keeps every stamp");
    }
    {   // a long session with a lost picture now and then never fills the list
        std::deque<Stamp> s; std::int64_t id = 0; bool refused = false;
        for (int frame = 0; frame < 100000; ++frame) {
            makeRoom(s); if (s.size() >= kMaxStamps) refused = true;
            s.push_back({id, 0});
            if (frame % 37 != 0) { prune(s, id, 0); s.pop_front(); }   // every 37th picture never comes out
            ++id;
        }
        check(!refused && s.size() <= 1, "100 000 pictures with one in 37 lost: the list never fills");
    }
    {   // a full list drops its oldest instead of refusing
        std::deque<Stamp> s; for (int i = 0; i < 70; ++i) s.push_back({i, i});
        check(makeRoom(s) == 7 && s.size() == kMaxStamps - 1 && s.front().id == 7, "a full list makes room by dropping its oldest");
        std::deque<Stamp> e; check(makeRoom(e) == 0, "an empty list has room");
    }
    {   // the card's decoder is given up only on a second failure without a picture between them
        HardwareStrikes h;
        check(!h.failed(), "one damaged picture keeps the card's decoder");
        h.decoded();
        check(!h.failed(), "a picture in between starts the count again");
        check(h.failed(), "two failures in a row give the card's decoder up");
        h.reset(); check(!h.failed(), "a new session starts the count again");
    }
    std::printf(failures ? "%d FAILED\n" : "ALL PASSED (%d failures)\n", failures);
    return failures ? 1 : 0;
}
