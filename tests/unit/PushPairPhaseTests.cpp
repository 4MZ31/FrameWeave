// PushFPS 4.5.1: the learned phase of a capture card's frame generation pairs (LivePairLatency) is kept
// through a gap in readings instead of going back to the whole legacy delay. Pure; also builds with g++.
#include "veyra/engine/LivePairLatency.h"
#include <cstdio>
#include <cstdlib>

using veyra::engine::LivePairLatency;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
    const int64_t ms = 10000, second = 10000000;
    const int64_t interval = 333333, legacy = interval + 40000, allowance = interval / 2;
    // Nothing read: the legacy delay, as always.
    {
        LivePairLatency p;
        CHECK(p.select(1, legacy, interval, 2, allowance) == legacy);
    }
    // Learn a phase of about 8 ms from 30 readings a second, then a two-second pause with none.
    LivePairLatency p;
    int64_t now = second, chosen = 0;
    for (int i = 0; i < 300; ++i) {
        now += interval;
        p.observe(now, 8 * ms);
        chosen = p.select(now, legacy, interval, 2, allowance);
    }
    CHECK(chosen == 8 * ms + LivePairLatency::kMargin100ns);
    now += 2 * second;
    const auto afterPause = p.select(now, legacy, interval, 2, allowance);
    CHECK(afterPause == chosen);            // was: the legacy delay (about 37 ms) again
    // A first reading after the pause that needs more raises it at once.
    p.observe(now, 15 * ms);
    CHECK(p.select(now + 1, legacy, interval, 2, allowance) == 15 * ms + LivePairLatency::kMargin100ns);
    // Never past the limit.
    p.observe(now + 2, 90 * ms);
    CHECK(p.select(now + 3, legacy, interval, 2, allowance) == legacy + allowance);
    // Generation off (multiplier 1) answers the legacy delay and forgets the phase.
    CHECK(p.select(now + 4, legacy, interval, 1, allowance) == legacy);
    {
        LivePairLatency q;
        q.observe(1, 8 * ms);
        CHECK(q.select(2, legacy, interval, 1, allowance) == legacy);
        CHECK(q.select(3, legacy, interval, 2, allowance) == legacy);   // starts again from the legacy delay
    }
    // reset() starts from the legacy delay again.
    p.reset();
    CHECK(p.select(now + 5, legacy, interval, 2, allowance) == legacy);
    // With 8 readings or more the rule is unchanged: down by at most a step a pair, up at once.
    {
        LivePairLatency q;
        int64_t t = second, last = 0;
        for (int i = 0; i < 8; ++i) { t += interval; q.observe(t, 5 * ms); }
        last = q.select(t, legacy, interval, 2, allowance);
        CHECK(last == legacy - 5000 || last == legacy);   // first step from the legacy delay
        const auto next = q.select(t, legacy, interval, 2, allowance);
        CHECK(next == last - 5000);
    }
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("pair phase tests passed\n");
    return 0;
}
