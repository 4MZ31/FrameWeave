#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS performance report: what a session really did, as text a user can copy into a bug report
// or a hardware report. The graphics card and its driver, the settings that matter, and the rates
// and delays PushFPS measured itself while it played: the game's own frames, the generated frames
// actually shown, what the screen got, and the software delay from a frame's arrival to its Present.
//
// Two rules.
//  - Nothing personal. Numbers, yes/no values and words from fixed lists only; the one free text, the
//    graphics card's name, is reduced to plain characters. Never a device name, a file, a folder, a
//    host, an address, an account or a key: the fields do not exist, so nothing can leak through one.
//  - Nothing claimed that was not measured. The delay is PushFPS's own software timing, not button to
//    screen, and the report says so; a rate that was not measured is null, never 0.
//
// Pure: no Windows or Qt types. The bridge fills the facts (bridge/PushTools.cpp); tests
// tests/unit/PushPerfReportTests.cpp.
#include "veyra/engine/PushCrashNote.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace veyra::engine::perfreport {

inline constexpr int kSchema = 1;

struct Facts {
    // The program and the PC.
    std::string version, windows;            // "3.2.5", "10.0.26100"
    std::string gpu, driver;                 // the card's name and its driver version
    uint64_t gpuMemoryMb = 0;
    bool softwareRenderer = false;           // Windows' software renderer: timings say nothing about a card
    double displayHz = 0;
    int vrrState = -1;                       // the bridge's vrrState (-1 unknown)
    // What plays.
    bool playing = false;
    std::string source;                      // capture, remoteplay, moonlight, xbox, file, url, screen, frameinput
    uint32_t sourceWidth = 0, sourceHeight = 0, outputWidth = 0, outputHeight = 0;
    bool hdr = false;
    double captureFps = 0;                   // capture sources: what the card delivers
    uint64_t captureReceived = 0, captureDropped = 0;
    int contentFps = 0;                      // distinct game pictures a second
    // Settings.
    bool fgOn = false;
    std::string fgMethod, fgTier;            // "pushflow", "balanced"
    uint32_t multiplier = 1, multiplierNow = 0;
    bool adaptive = false, pacingRepair = false, hudLock = false;
    int contentRate = 0;                     // duplicate removal mode (engine::ContentRate)
    bool aiEnhancement = false, upscaling = false;
    // Measured.
    bool fgActive = false, budgetLimited = false;
    int fgHealth = 0;                        // engine::PushFgIdle
    bool ratesReady = false;                 // the one-second rate window is full
    double realFps = 0, generatedFps = 0;    // frames really presented a second
    std::optional<double> presentedFps, low1Fps, latencyMs, latencyP95Ms, gpuPercent;
    uint64_t latencySamples = 0;
    double pushFlowP50Ms = 0, pushFlowP95Ms = 0;
    std::string pushFlowPrecision;           // "fp32", "fp16"
    int ghostGuard = 0;                      // PushFlow's ghost guard in use: 0 off, 1 careful, 2 standard, 3 strong
    std::optional<double> ghostGuardShare;   // the share of recent generated frames it changed (0..1)
};

// A word from a fixed list: lower-case letters, digits and '-' only, else empty.
inline std::string word(std::string_view value, size_t max = 24) {
    std::string out;
    for (const char c : value) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok || out.size() >= max) return {};
        out.push_back(c);
    }
    return out;
}
// A version number: digits and dots.
inline std::string versionText(std::string_view value, size_t max = 32) {
    std::string out;
    for (const char c : value) {
        const bool ok = (c >= '0' && c <= '9') || c == '.';
        if (!ok || out.size() >= max) return {};
        out.push_back(c);
    }
    return out;
}
// The program's own version: "3.2.5", or a test build's "2.1.5-pushfps.812".
inline std::string buildText(std::string_view value, size_t max = 32) {
    std::string out;
    for (const char c : value) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '.' || c == '-';
        if (!ok || out.size() >= max) return {};
        out.push_back(c);
    }
    return out;
}
// A graphics card's name as its driver gives it ("AMD Radeon RX 7900 XT/XTX", "Intel(R) Arc(TM)"),
// reduced to plain characters. A value with a backslash, a colon or an @ is no card name (a path, an
// address): dropped whole.
inline std::string cardName(std::string_view value, size_t max = 64) {
    for (const char c : value)
        if (c == '\\' || c == ':' || c == '@' || static_cast<unsigned char>(c) < 0x20) return {};
    return crashnote::plainText(value, max);
}
namespace detail {
inline std::string quoted(const std::string& text) { return text.empty() ? std::string("null") : "\"" + text + "\""; }
inline std::string number(double value, int decimals = 1) {
    if (!std::isfinite(value)) return "null";
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    return buffer;
}
inline std::string number(const std::optional<double>& value, int decimals = 1) { return value ? number(*value, decimals) : std::string("null"); }
inline const char* yes(bool value) { return value ? "true" : "false"; }
} // namespace detail

// What the measured numbers are worth, in the report's own words.
inline const char* evidence(const Facts& f) {
    if (f.softwareRenderer) return "software renderer: correctness only, timings say nothing about a graphics card";
    if (!f.playing) return "nothing was playing: hardware and settings only";
    if (!f.ratesReady) return "live session, rates not settled yet";
    return "live session, PushFPS's own software timing";
}

// The report as JSON text (two-space indent, fixed key order).
inline std::string json(const Facts& f) {
    using namespace detail;
    const std::string gpu = cardName(f.gpu);
    std::string o = "{\n";
    o += "  \"report\": \"pushfps-performance\",\n";
    o += "  \"schema\": " + std::to_string(kSchema) + ",\n";
    o += "  \"evidence\": \"" + std::string(evidence(f)) + "\",\n";
    o += "  \"version\": " + quoted(buildText(f.version)) + ",\n";
    o += "  \"windows\": " + quoted(versionText(f.windows)) + ",\n";
    o += "  \"gpu\": { \"name\": " + quoted(gpu) + ", \"driver\": " + quoted(versionText(f.driver)) + ", \"memoryMb\": " + std::to_string(f.gpuMemoryMb) +
         ", \"softwareRenderer\": " + yes(f.softwareRenderer) + " },\n";
    o += "  \"display\": { \"refreshHz\": " + number(f.displayHz, 1) + ", \"vrrState\": " + std::to_string(f.vrrState) + " },\n";
    o += "  \"playing\": " + std::string(yes(f.playing)) + ",\n";
    o += "  \"source\": { \"kind\": " + quoted(word(f.source)) + ", \"width\": " + std::to_string(f.sourceWidth) + ", \"height\": " + std::to_string(f.sourceHeight) +
         ", \"hdr\": " + yes(f.hdr) + ", \"captureFps\": " + number(f.captureFps, 2) + ", \"captureReceived\": " + std::to_string(f.captureReceived) +
         ", \"captureDropped\": " + std::to_string(f.captureDropped) + ", \"contentFps\": " + std::to_string(f.contentFps) + " },\n";
    o += "  \"output\": { \"width\": " + std::to_string(f.outputWidth) + ", \"height\": " + std::to_string(f.outputHeight) + " },\n";
    o += "  \"frameGeneration\": { \"on\": " + std::string(yes(f.fgOn)) + ", \"method\": " + quoted(word(f.fgMethod)) + ", \"tier\": " + quoted(word(f.fgTier)) +
         ", \"multiplier\": " + std::to_string(f.multiplier) + ", \"multiplierNow\": " + std::to_string(f.multiplierNow) + ", \"adaptive\": " + yes(f.adaptive) +
         ", \"duplicateRemoval\": " + std::to_string(f.contentRate) + ", \"pacingRepair\": " + yes(f.pacingRepair) + ", \"hudLock\": " + yes(f.hudLock) + " },\n";
    o += "  \"enhancement\": { \"aiPicture\": " + std::string(yes(f.aiEnhancement)) + ", \"upscaling\": " + yes(f.upscaling) + " },\n";
    // Rates before the window is full would read as a fault that is not there.
    const bool rates = f.playing && f.ratesReady;
    o += "  \"measured\": {\n";
    o += "    \"realFps\": " + (rates ? number(f.realFps) : std::string("null")) + ",\n";
    o += "    \"generatedFps\": " + (rates ? number(f.generatedFps) : std::string("null")) + ",\n";
    o += "    \"presentedFps\": " + (rates ? number(f.presentedFps) : std::string("null")) + ",\n";
    o += "    \"low1Fps\": " + (rates ? number(f.low1Fps) : std::string("null")) + ",\n";
    o += "    \"softwareLatencyMs\": " + (f.playing ? number(f.latencyMs) : std::string("null")) + ",\n";
    o += "    \"softwareLatencyP95Ms\": " + (f.playing ? number(f.latencyP95Ms) : std::string("null")) + ",\n";
    o += "    \"latencySamples\": " + std::to_string(f.playing ? f.latencySamples : 0) + ",\n";
    o += "    \"gpuPercent\": " + number(f.gpuPercent, 0) + ",\n";
    o += "    \"frameGenerationActive\": " + std::string(yes(f.fgActive)) + ",\n";
    o += "    \"frameGenerationSkipping\": " + std::string(yes(f.budgetLimited)) + ",\n";
    o += "    \"frameGenerationHealth\": " + std::to_string(f.fgHealth) + ",\n";
    o += "    \"pushFlowMs\": " + (f.pushFlowP50Ms > 0 ? number(f.pushFlowP50Ms) : std::string("null")) + ",\n";
    o += "    \"pushFlowP95Ms\": " + (f.pushFlowP95Ms > 0 ? number(f.pushFlowP95Ms) : std::string("null")) + ",\n";
    o += "    \"pushFlowPrecision\": " + quoted(word(f.pushFlowPrecision)) + ",\n";
    o += "    \"ghostGuard\": " + std::to_string(std::clamp(f.ghostGuard, 0, 3)) + ",\n";
    o += "    \"ghostGuardShare\": " + (f.ghostGuard > 0 && f.ghostGuardShare && *f.ghostGuardShare >= 0 && *f.ghostGuardShare <= 1 ? number(*f.ghostGuardShare, 3) : std::string("null")) + "\n";
    o += "  },\n";
    o += "  \"notes\": \"Rates are frames PushFPS presented in the last second. softwareLatency is a frame's arrival in PushFPS to its Present call: "
         "not button to screen, and it leaves out the capture card, the console and the display. frameGenerationHealth: 0 fine, 1 nothing to add at this "
         "target or on this screen, 2 the method is not running, 3 the card does not keep up, 4 nothing generated is shown. ghostGuard (experimental): 0 off, 1 careful, "
         "2 standard, 3 strong; ghostGuardShare is the part of recent generated frames it changed.\"\n";
    o += "}\n";
    return o;
}

} // namespace veyra::engine::perfreport
