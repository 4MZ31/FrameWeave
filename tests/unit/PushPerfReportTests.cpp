// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The performance report (engine/PushPerfReport.h): it says what was measured, it says what the
// numbers are worth, and nothing personal can get into it whatever the facts hold.
#include "veyra/engine/PushPerfReport.h"
#include <cstdio>
#include <string>
using namespace veyra::engine::perfreport;
static int failures = 0;
static void check(bool ok, const std::string& label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str()); if (!ok) ++failures; }
static bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

static Facts session() {
    Facts f;
    f.version = "3.2.5"; f.windows = "10.0.26100"; f.gpu = "NVIDIA GeForce RTX 5090"; f.driver = "32.0.15.7652"; f.gpuMemoryMb = 32607;
    f.displayHz = 120; f.vrrState = 2; f.playing = true; f.source = "capture";
    f.sourceWidth = 1920; f.sourceHeight = 1080; f.outputWidth = 3840; f.outputHeight = 2160; f.hdr = false;
    f.captureFps = 59.94; f.captureReceived = 36000; f.captureDropped = 3; f.contentFps = 30;
    f.fgOn = true; f.fgMethod = "pushflow"; f.fgTier = "balanced"; f.multiplier = 4; f.multiplierNow = 4; f.contentRate = 1;
    f.aiEnhancement = true; f.upscaling = true; f.fgActive = true; f.ratesReady = true;
    f.realFps = 30.0; f.generatedFps = 89.6; f.presentedFps = 119.6; f.low1Fps = 112.4; f.latencyMs = 41.27; f.latencyP95Ms = 47.9; f.latencySamples = 1800;
    f.gpuPercent = 71.4; f.pushFlowP50Ms = 6.12; f.pushFlowP95Ms = 7.31; f.pushFlowPrecision = "fp32";
    return f;
}
// A JSON text is balanced and has no control characters or stray quotes (a cheap well-formedness check).
static bool wellFormed(const std::string& text) {
    int depth = 0; bool inString = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (static_cast<unsigned char>(c) < 0x20 && c != '\n') return false;
        if (c == '\\') return false;            // nothing in the report needs an escape
        if (c == '"') inString = !inString;
        else if (!inString && c == '{') ++depth;
        else if (!inString && c == '}') { if (--depth < 0) return false; }
    }
    return depth == 0 && !inString;
}

int main() {
    const std::string r = json(session());
    std::printf("%s", r.c_str());
    check(wellFormed(r), "the report is well-formed text");
    check(has(r, "\"report\": \"pushfps-performance\"") && has(r, "\"schema\": 1"), "it says what it is");
    check(has(r, "\"version\": \"3.2.5\"") && has(r, "\"windows\": \"10.0.26100\""), "the program and Windows versions");
    check(has(r, "\"name\": \"NVIDIA GeForce RTX 5090\"") && has(r, "\"driver\": \"32.0.15.7652\"") && has(r, "\"memoryMb\": 32607"), "the card, its driver and memory");
    check(has(r, "\"kind\": \"capture\"") && has(r, "\"width\": 1920") && has(r, "\"captureFps\": 59.94") && has(r, "\"captureDropped\": 3") && has(r, "\"contentFps\": 30"), "the source");
    check(has(r, "\"output\": { \"width\": 3840, \"height\": 2160 }"), "the output size");
    check(has(r, "\"method\": \"pushflow\"") && has(r, "\"tier\": \"balanced\"") && has(r, "\"multiplier\": 4") && has(r, "\"multiplierNow\": 4"), "the frame generation settings");
    check(has(r, "\"realFps\": 30.0") && has(r, "\"generatedFps\": 89.6") && has(r, "\"presentedFps\": 119.6") && has(r, "\"low1Fps\": 112.4"), "real, generated and presented frames a second");
    check(has(r, "\"softwareLatencyMs\": 41.3") && has(r, "\"softwareLatencyP95Ms\": 47.9") && has(r, "\"latencySamples\": 1800"), "the software delay with its sample count");
    check(has(r, "\"pushFlowMs\": 6.1") && has(r, "\"pushFlowP95Ms\": 7.3") && has(r, "\"pushFlowPrecision\": \"fp32\""), "PushFlow's own time per frame");
    check(has(r, "\"gpuPercent\": 71"), "the card's load");
    check(has(r, "\"ghostGuard\": 0") && has(r, "\"ghostGuardShare\": null"), "the ghost guard is off unless it was turned on, and then has no share");
    { auto f = session(); f.ghostGuard = 2; f.ghostGuardShare = 0.0372; const auto t = json(f);
      check(has(t, "\"ghostGuard\": 2") && has(t, "\"ghostGuardShare\": 0.037") && wellFormed(t), "the ghost guard's level and the share of generated frames it changed");
      f.ghostGuard = 77; f.ghostGuardShare = 5.0; const auto u = json(f);
      check(has(u, "\"ghostGuard\": 3") && has(u, "\"ghostGuardShare\": null"), "a level or share that cannot be is not passed on"); }
    // What the numbers are worth.
    check(has(r, "\"evidence\": \"live session, PushFPS's own software timing\""), "a live session is called that");
    check(has(r, "not button to screen"), "and the delay is not passed off as button to screen");
    { auto f = session(); f.softwareRenderer = true; const auto t = json(f);
      check(has(t, "software renderer: correctness only") && has(t, "\"softwareRenderer\": true"), "the software renderer is named: its timings mean nothing for a card"); }
    { auto f = session(); f.playing = false; const auto t = json(f);
      check(has(t, "nothing was playing") && has(t, "\"realFps\": null") && has(t, "\"generatedFps\": null") && has(t, "\"softwareLatencyMs\": null") && has(t, "\"latencySamples\": 0"),
            "nothing playing: no rates and no delay, never zeros"); }
    { auto f = session(); f.ratesReady = false; const auto t = json(f);
      check(has(t, "rates not settled yet") && has(t, "\"realFps\": null") && has(t, "\"generatedFps\": null"), "rates before the window is full are left out"); }
    { auto f = session(); f.presentedFps.reset(); f.low1Fps.reset(); f.latencyMs.reset(); f.latencyP95Ms.reset(); f.gpuPercent.reset(); f.pushFlowP50Ms = 0; f.pushFlowP95Ms = 0; f.pushFlowPrecision.clear();
      const auto t = json(f);
      check(has(t, "\"presentedFps\": null") && has(t, "\"low1Fps\": null") && has(t, "\"softwareLatencyMs\": null") && has(t, "\"gpuPercent\": null") &&
            has(t, "\"pushFlowMs\": null") && has(t, "\"pushFlowPrecision\": null"), "what was not measured is null");
      check(wellFormed(t), "and the text stays well-formed"); }
    { auto f = session(); f.fgHealth = 4; f.fgActive = false; f.budgetLimited = true; const auto t = json(f);
      check(has(t, "\"frameGenerationHealth\": 4") && has(t, "\"frameGenerationActive\": false") && has(t, "\"frameGenerationSkipping\": true"), "a frame generation fault is in the report"); }
    { auto f = session(); f.latencyMs = std::nan(""); f.realFps = INFINITY; const auto t = json(f);
      check(has(t, "\"softwareLatencyMs\": null") && has(t, "\"realFps\": null") && wellFormed(t), "a value that is no number becomes null"); }

    // Nothing personal, whatever the facts hold.
    {
        auto f = session();
        f.gpu = "NVIDIA \"RTX\" C:\\Users\\Jan Jansen\\card";
        f.driver = "32.0 C:\\Users\\Jan";
        f.version = "3.2.5 (Jan's build)";
        f.windows = "10.0.26100 JAN-PC";
        f.source = "C:\\Users\\Jan\\video.mp4";
        f.fgMethod = "pushflow\", \"user\": \"jan";
        f.fgTier = "Balanced by Jan";
        f.pushFlowPrecision = "fp32 192.168.1.20";
        const auto t = json(f);
        check(wellFormed(t), "odd values cannot break the text");
        check(!has(t, "Jan") && !has(t, "jan") && !has(t, "Users") && !has(t, "192.168") && !has(t, ".mp4") && !has(t, "JAN-PC"), "nor carry a name, a path, a file or an address out");
        check(has(t, "\"name\": null") && has(t, "\"driver\": null") && has(t, "\"version\": null") && has(t, "\"windows\": null") && has(t, "\"kind\": null") &&
              has(t, "\"method\": null") && has(t, "\"tier\": null") && has(t, "\"pushFlowPrecision\": null"), "a value that is not what it should be is dropped whole");
        check(!has(t, "\\") && !has(t, "C:"), "no path characters at all");
    }
    check(word("pushflow") == "pushflow" && word("fsr3") == "fsr3" && word("60-30") == "60-30", "words from the fixed lists pass");
    check(word("PushFlow").empty() && word("a b").empty() && word("a/b").empty() && word("user@host").empty() && word(std::string(40, 'a')).empty(), "anything else does not");
    check(versionText("32.0.15.7652") == "32.0.15.7652" && versionText("3.2.5") == "3.2.5", "version numbers pass");
    check(versionText("3.2.5-beta").empty() && versionText("1.0 x").empty() && versionText(std::string(40, '1')).empty(), "anything else does not");
    check(buildText("3.2.5") == "3.2.5" && buildText("2.1.5-pushfps.812") == "2.1.5-pushfps.812", "the program's version, also a test build's");
    check(buildText("3.2.5 (Jan)").empty() && buildText("3.2.5/x").empty() && buildText("V3").empty(), "and nothing else");
    { auto f = session(); f.version = "2.1.5-pushfps.812"; check(has(json(f), "\"version\": \"2.1.5-pushfps.812\""), "a test build's version is in the report"); }
    // The card's name keeps what a card name has.
    { auto f = session(); f.gpu = "AMD Radeon RX 9070 XT"; check(has(json(f), "\"name\": \"AMD Radeon RX 9070 XT\""), "an AMD card's name"); }
    { auto f = session(); f.gpu = "Intel(R) Arc(TM) B580 Graphics"; check(has(json(f), "\"name\": \"Intel(R) Arc(TM) B580 Graphics\""), "an Intel card's name"); }
    { auto f = session(); f.gpu.clear(); check(has(json(f), "\"name\": null"), "no card name: null"); }
    { auto f = session(); f.gpu = "AMD Radeon RX 7900 XT/XTX"; check(has(json(f), "\"name\": \"AMD Radeon RX 7900 XT XTX\""), "a slash in a card's name becomes a space"); }
    check(cardName("NVIDIA GeForce RTX 4070").size() == 23 && cardName("x@y").empty() && cardName("C:\\card").empty() && cardName("a\tb").empty(), "what is no card name is dropped whole");
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
