// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Asking a device driver without waiting for ever (source/PushTimedProbe.h). Pure: also builds with g++.
#include "veyra/source/PushTimedProbe.h"
#include <atomic>
#include <cstdio>
#include <string>
using namespace veyra::source;
using namespace std::chrono_literals;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::printf("FAILED line %d: %s\n", __LINE__, #x); } } while (0)

int main() {
    using Probes = PushTimedProbes<std::wstring, int>;
    using O = Probes::Outcome;
    Probes probes;

    // A device that answers at once: its answer, and nothing left waiting.
    auto r = probes.ask(L"a", [] { return 7; }, 500ms);
    CHECK(r.outcome == O::Answered && r.value == 7);
    CHECK(probes.waiting() == 0);

    // Asked again: asked afresh, not an old answer.
    std::atomic<int> calls{0};
    r = probes.ask(L"a", [&] { return ++calls; }, 500ms);
    CHECK(r.outcome == O::Answered && r.value == 1 && calls == 1);

    // A device that does not answer within the limit: the list goes on after the limit, not later.
    std::atomic<bool> release{false};
    const auto start = std::chrono::steady_clock::now();
    r = probes.ask(L"stuck", [&] { while (!release) std::this_thread::sleep_for(5ms); return 3; }, 100ms);
    const auto took = std::chrono::steady_clock::now() - start;
    CHECK(r.outcome == O::NoAnswer);
    CHECK(took >= 100ms && took < 1000ms);
    CHECK(probes.waiting() == 1);

    // Another device is asked meanwhile and answers.
    r = probes.ask(L"b", [] { return 9; }, 500ms);
    CHECK(r.outcome == O::Answered && r.value == 9);

    // The stuck one is not asked a second time while its first question waits, and the caller is not held.
    calls = 0;
    const auto again = std::chrono::steady_clock::now();
    r = probes.ask(L"stuck", [&] { ++calls; return 0; }, 500ms);
    CHECK(r.outcome == O::StillWaiting);
    CHECK(calls == 0);
    CHECK(std::chrono::steady_clock::now() - again < 100ms);

    // Its late answer is handed out once at the next question, then the key is asked afresh.
    release = true;
    for (int i = 0; i < 200 && probes.waiting() != 0; ++i) std::this_thread::sleep_for(5ms);
    CHECK(probes.waiting() == 0);
    r = probes.ask(L"stuck", [&] { ++calls; return 0; }, 500ms);
    CHECK(r.outcome == O::AnsweredEarlier && r.value == 3 && calls == 0);
    r = probes.ask(L"stuck", [&] { ++calls; return 5; }, 500ms);
    CHECK(r.outcome == O::Answered && r.value == 5 && calls == 1);

    // A question that throws counts as answered with the empty value; nothing escapes.
    r = probes.ask(L"throws", []() -> int { throw 1; }, 500ms);
    CHECK(r.outcome == O::Answered && r.value == 0);

    // Many devices in a row, one of them stuck: each other device answers, the whole list is bounded.
    std::atomic<bool> release2{false};
    const auto listStart = std::chrono::steady_clock::now();
    int answered = 0;
    for (int i = 0; i < 6; ++i) {
        const auto key = L"dev" + std::to_wstring(i);
        auto rr = i == 2 ? probes.ask(key, [&] { while (!release2) std::this_thread::sleep_for(5ms); return 1; }, 80ms)
                         : probes.ask(key, [] { return 1; }, 80ms);
        if (rr.outcome == O::Answered) ++answered;
    }
    CHECK(answered == 5);
    CHECK(std::chrono::steady_clock::now() - listStart < 1000ms);
    release2 = true;
    for (int i = 0; i < 200 && probes.waiting() != 0; ++i) std::this_thread::sleep_for(5ms);
    CHECK(probes.waiting() == 0);

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("push timed probe tests passed\n");
    return 0;
}
