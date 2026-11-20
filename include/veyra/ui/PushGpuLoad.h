#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The graphics card's load, from Windows' "GPU Engine" counters, counted as Task Manager counts it.
//
// A counter instance is one process on one engine of one adapter:
//   pid_<n>_luid_0x<high>_0x<low>_phys_<p>_eng_<e>_engtype_<type>
// A card has several engines, and more than one of some types (NVIDIA cards list a handful of Copy and
// Compute engines beside 3D, VideoDecode and VideoEncode). An engine's load is the sum over the processes
// using it, and the card's load is its busiest engine.
// Until this was written the sum was taken per engine TYPE: every Copy engine added together, every
// Compute engine added together. PushFPS spreads copies and compute over several of them, so the figure
// could read 100 % on a card whose monitoring tools showed a quarter of that (the owner, 20 November 2026:
// "it says my GPU is at 100 % but my actual monitoring says 23 %"). It is a readout only: nothing in the
// engine acts on it.
// Pure: no counter is read here.
#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <vector>
namespace veyra::ui {
struct PushGpuEngineSample { std::wstring instance; double percent = 0; };   // instance: lower case
// The engine an instance belongs to: the adapter, its physical index, the engine's index and type.
// The whole name when it has no adapter part (an unknown format still counts as one engine of its own).
inline std::wstring_view pushGpuEngineOf(std::wstring_view instance) {
    const auto at = instance.find(L"luid_");
    return at == std::wstring_view::npos ? instance : instance.substr(at);
}
// adapter: "luid_0x<high>_0x<low>_" in lower case to count one adapter only, empty for all.
inline double pushGpuLoad(const std::vector<PushGpuEngineSample>& samples, std::wstring_view adapter = {}) {
    std::map<std::wstring, double, std::less<>> byEngine;
    for (const auto& s : samples) {
        if (!adapter.empty() && s.instance.find(adapter) == std::wstring::npos) continue;
        const auto engine = pushGpuEngineOf(s.instance);
        const auto it = byEngine.find(engine);
        if (it == byEngine.end()) byEngine.emplace(std::wstring(engine), s.percent); else it->second += s.percent;
    }
    double busiest = 0;
    for (const auto& [engine, percent] : byEngine) busiest = std::max(busiest, percent);
    return std::clamp(busiest, 0.0, 100.0);
}
}
