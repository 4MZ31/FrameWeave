// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The crash summary (engine/PushCrashNote.h): what the crash handler's note says, and above all that
// nothing personal can get into the text the user copies, whatever the note or the facts hold.
#include "veyra/engine/PushCrashNote.h"
#include <cstdio>
#include <string>
using namespace veyra::engine::crashnote;
static int failures = 0;
static void check(bool ok, const char* label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label); if (!ok) ++failures; }
static bool has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

int main() {
    // The note as 3.2.2 writes it.
    const Marker m = parseMarker("crash onnxruntime.dll\nRTSSHooks64.dll,gameoverlayrenderer64.dll\n"
                                 "code=0xC0000005 offset=0x11B640 dump=veyra-crash-20261118-211405-1234.dmp time=2026-11-18 21:14\n");
    check(m.present && m.crash, "a crash note is a crash");
    check(m.module == "onnxruntime.dll", "the module's file name");
    check(m.injected == "RTSSHooks64.dll, gameoverlayrenderer64.dll", "injected components listed");
    check(m.code == "0xC0000005" && m.offset == "0x11B640", "exception code and offset");
    check(m.dump == "veyra-crash-20261118-211405-1234.dmp", "the crash file's name");
    check(m.time == "2026-11-18 21:14", "when");
    // The note of 3.2.1 and earlier: two lines.
    const Marker old = parseMarker("crash nvngx_dlssg.dll\r\n\r\n");
    check(old.crash && old.module == "nvngx_dlssg.dll" && old.code.empty() && old.dump.empty() && old.injected.empty(), "an older note without the third line");
    const Marker device = parseMarker("device\nRTSSHooks64.dll\n");
    check(device.present && !device.crash, "a lost graphics device is not a crash");
    check(!parseMarker("").present && !parseMarker("\n\n").present, "an empty note is no note");
    check(parseMarker("crash").crash && parseMarker("crash").module.empty() && parseMarker("crash ").module.empty(), "a crash outside any module");

    // Nothing personal: full paths lose their folders, and anything odd is dropped.
    const Marker path = parseMarker("crash C:\\Users\\Jan Jansen\\AppData\\Local\\PushFPS\\runtime\\onnxruntime.dll\n"
                                    "C:\\Program Files\\RivaTuner\\RTSSHooks64.dll\n"
                                    "code=0xC0000005 offset=0x10 dump=C:\\Users\\Jan Jansen\\logs\\veyra-crash-1.dmp time=2026-11-18 21:14\n");
    check(path.module == "onnxruntime.dll" && path.injected == "RTSSHooks64.dll", "a module given with its path keeps only the file name");
    check(path.dump.empty(), "a crash file given with a path is not the handler's own name: left out");
    check(parseMarker("crash x.dll\n\ncode=0x1 offset=0x2 dump=notes.txt time=2026-11-18 21:14").dump.empty(), "nor is a file that is not a .dmp");
    check(safeField("C:\\Users\\me").empty() && safeField("\\\\server\\share").empty() && safeField("a/b").empty(), "safeField drops paths");
    check(safeField("192.168.1.20:9295").empty() && safeField("user@example.com").empty() && safeField("https://x.y").empty(), "and addresses");
    check(safeField("  PushFlow 3X  ") == "PushFlow 3X" && safeField(std::string(100, 'a')).size() == 64, "plain text is kept, trimmed and capped");
    check(safeField(std::string("a\nb")).empty() && safeField(std::string("a\0b", 3)).empty(), "no control characters");
    check(safeFileName("..\\..\\x.dll") == "x.dll" && safeFileName("C:\\Users\\me\\") .empty(), "safeFileName keeps the last part only");
    check(safeHex("0xC0000005") == "0xC0000005" && safeHex("C0000005").empty() && safeHex("0xZZ").empty() && safeHex("0x").empty(), "safeHex");
    check(safeTime("2026-11-18 21:14") == "2026-11-18 21:14" && safeTime("2026-11-18T21:14").empty() && safeTime("yesterday").empty(), "safeTime");
    check(plainText("Intel(R) Arc(TM) A770 Graphics") == "Intel(R) Arc(TM) A770 Graphics", "a graphics card name is kept");
    check(plainText("AMD Radeon RX 7900 XT/XTX") == "AMD Radeon RX 7900 XT XTX" && plainText("C:\\Users\\me") == "C Users me", "other characters become spaces");

    // The session note: choices and numbers, one line.
    const std::string note = sessionNote({{"source", "xbox"}, {"size", "1920x1080"}, {"fg", "pushflow 3X fast"}, {"nr", "1"}, {"sr", "0"},
                                          {"hdr", "0"}, {"host", "192.168.1.20"}, {"label", "C:\\Users\\me\\clip.mp4"}, {"empty", ""}});
    check(note == "source=xbox size=1920x1080 fg=pushflow_3X_fast nr=1 sr=0 hdr=0", "the session note keeps plain values and drops the rest");
    check(sessionValue(note, "fg") == "pushflow_3X_fast" && sessionValue(note, "size") == "1920x1080" && sessionValue(note, "host").empty(), "values read back");
    check(sessionValue(note, "s").empty() && sessionValue("", "source").empty() && sessionValue("fgx=1 fg=2", "fg") == "2", "a key is matched whole");
    check(sessionNote({{"source", "192.168.1.20"}, {"size", "1920x1080x3"}, {"size", "x1080"}, {"fg", "a.b"}, {"nr", "2"}, {"hdr", "yes"}}).empty(), "values of the wrong shape are left out");
    check(sessionValue("source=xbox size=C:x", "size").empty() && sessionValue("source=a.b", "source").empty(), "and not read back either");

    // The summary.
    Facts f;
    f.version = "3.2.2"; f.windows = "10.0.26100"; f.gpu = "NVIDIA GeForce RTX 5090"; f.session = note; f.optimise = "pushflow";
    const std::string text = summary(m, f);
    check(has(text, "PushFPS crash summary\nVersion: 3.2.2\n"), "summary: version");
    check(has(text, "Crashed in: onnxruntime.dll +0x11B640 (code 0xC0000005)\n"), "summary: where");
    check(has(text, "Time: 2026-11-18 21:14\n") && has(text, "Windows: 10.0.26100\n") && has(text, "Graphics card: NVIDIA GeForce RTX 5090\n"), "summary: the PC");
    check(has(text, "Playing: xbox 1920x1080\n") && has(text, "Frame generation: pushflow 3X fast\n"), "summary: what was playing");
    check(has(text, "AI picture enhancement: on\n") && has(text, "Upscaling: off\n"), "summary: enhancement and upscaling");
    check(has(text, "Optimise for my PC: was running, testing pushflow\n"), "summary: the optimiser's step");
    check(has(text, "Injected: RTSSHooks64.dll, gameoverlayrenderer64.dll\n") && has(text, "Crash file: veyra-crash-20261118-211405-1234.dmp\n"), "summary: injected and crash file");
    // Hostile facts and a hostile note cannot put a path, a user name or an address into it.
    Facts bad;
    bad.version = "3.2.2 C:\\Users\\Jan Jansen"; bad.windows = "\\\\PC-JAN\\share"; bad.gpu = "GPU of C:\\Users\\Jan Jansen";
    bad.session = "source=C:\\Users\\Jan size=192.168.1.20:9295 fg=..\\..\\x nr=1"; bad.optimise = "C:\\Users\\Jan Jansen\\x";
    const std::string scrubbed = summary(path, bad);
    check(!has(scrubbed, "\\") && !has(scrubbed, "/") && !has(scrubbed, "192.168") && !has(scrubbed, "Jansen\\"), "summary: no path separators or addresses survive");
    check(!has(scrubbed, "Users\\") && has(scrubbed, "Version: unknown\n") && has(scrubbed, "Windows: unknown\n"), "summary: a value that is not plain is replaced by unknown");
    check(has(scrubbed, "Playing: nothing recorded\n") && !has(scrubbed, "Optimise for my PC"), "summary: a bad session note says nothing");
    // Nothing known at all still gives a readable summary.
    const std::string bare = summary(Marker{}, Facts{});
    check(has(bare, "Crashed in: unknown\n") && has(bare, "Crash file: not recorded\n") && has(bare, "Playing: nothing recorded\n"), "summary: empty input");
    // A source with frame generation off, HDR on.
    Facts off; off.session = sessionNote({{"source", "capture"}, {"size", "3840x2160"}, {"fg", "off"}, {"nr", "0"}, {"sr", "1"}, {"hdr", "1"}});
    const std::string plain = summary(old, off);
    check(has(plain, "Playing: capture 3840x2160 HDR\n") && has(plain, "Frame generation: off\n") && has(plain, "Upscaling: on\n"), "summary: capture card, HDR, no frame generation");
    check(has(plain, "Crashed in: nvngx_dlssg.dll\n") && has(plain, "Time: unknown\n"), "summary: an older note");

    std::printf("%s\n", failures ? "PushCrashNote tests FAILED" : "PushCrashNote tests passed");
    return failures ? 1 : 0;
}
