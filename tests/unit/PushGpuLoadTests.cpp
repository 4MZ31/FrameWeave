// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The graphics card's load as Task Manager counts it (ui/PushGpuLoad.h). Pure: also builds with g++.
#include "veyra/ui/PushGpuLoad.h"
#include <cmath>
#include <cstdio>
using namespace veyra::ui;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::printf("FAILED line %d: %s\n", __LINE__, #x); } } while (0)
static bool nearly(double a, double b) { return std::fabs(a - b) < 1e-9; }
int main() {
    const std::wstring card = L"luid_0x00000000_0x0000d2b1_", other = L"luid_0x00000000_0x00011f40_";
    auto name = [](const std::wstring& adapter, int pid, int engine, const wchar_t* type) {
        return L"pid_" + std::to_wstring(pid) + L"_" + adapter + L"phys_0_eng_" + std::to_wstring(engine) + L"_engtype_" + type;
    };
    {   // The report: 3D at 23 %, and PushFPS's copies and compute spread over several engines of one type.
        const std::vector<PushGpuEngineSample> s{
            {name(card, 100, 0, L"3d"), 20}, {name(card, 200, 0, L"3d"), 3},
            {name(card, 100, 4, L"copy"), 22}, {name(card, 100, 5, L"copy"), 18}, {name(card, 100, 6, L"copy"), 21}, {name(card, 100, 7, L"copy"), 19}, {name(card, 300, 8, L"copy"), 20},
            {name(card, 100, 9, L"compute_0"), 15}, {name(card, 100, 10, L"compute_0"), 12}};
        CHECK(nearly(pushGpuLoad(s), 23));   // the busiest engine, not every Copy engine added together (that read 100)
        CHECK(nearly(pushGpuLoad(s, card), 23));
    }
    {   // One engine's load is the sum over the processes using it.
        const std::vector<PushGpuEngineSample> s{{name(card, 1, 0, L"3d"), 40}, {name(card, 2, 0, L"3d"), 35}, {name(card, 3, 1, L"copy"), 50}};
        CHECK(nearly(pushGpuLoad(s), 75));
    }
    {   // Only the chosen adapter counts when one is given; all of them otherwise.
        const std::vector<PushGpuEngineSample> s{{name(card, 1, 0, L"3d"), 30}, {name(other, 1, 0, L"3d"), 90}};
        CHECK(nearly(pushGpuLoad(s, card), 30) && nearly(pushGpuLoad(s, other), 90) && nearly(pushGpuLoad(s), 90));
        CHECK(nearly(pushGpuLoad(s, L"luid_0x00000000_0x00000001_"), 0));
    }
    {   // Never above 100, never below 0, nothing for nothing, and a name without an adapter is its own engine.
        CHECK(nearly(pushGpuLoad({{name(card, 1, 0, L"3d"), 70}, {name(card, 2, 0, L"3d"), 70}}), 100));
        CHECK(nearly(pushGpuLoad({}), 0));
        CHECK(nearly(pushGpuLoad({{L"something_else", 12}, {L"something_else", 5}, {L"another", 9}}), 17));
        CHECK(pushGpuEngineOf(name(card, 77, 3, L"videodecode")) == card + L"phys_0_eng_3_engtype_videodecode");
    }
    std::printf(failures ? "%d FAILED\n" : "ALL PASSED (0 failures)\n", failures);
    return failures ? 1 : 0;
}
