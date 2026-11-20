// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// What the original's side of a comparison clip is called (engine/PushCompareName.h): the chosen
// console's name, never one that nobody chose. Pure: builds anywhere.
#include "veyra/engine/PushCompareName.h"
#include <cstdio>
#include <string>
using namespace veyra::engine;
static int failures = 0;
static void check(bool ok, const std::string& label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str()); if (!ok) ++failures; }
static std::string name(std::string_view kind, std::string_view console) {
    const auto n = pushCompareSourceName(kind, console);
    return n.side == PushCompareSide::Original ? "<original>" : n.side == PushCompareSide::Console ? "<console>" : n.name;
}
int main() {
    check(name("capture", "ps5") == "PS5" && name("capture", "ps5pro") == "PS5 Pro" && name("capture", "xbox") == "Xbox Series X|S" &&
          name("capture", "switch") == "Switch" && name("capture", "switch2") == "Switch 2", "a capture card: the console chosen in quick setup");
    check(name("capture", "") == "<console>", "a console nobody chose is not named (quick setup shows PlayStation 5 before anything is chosen)");
    check(name("capture", "other") == "<console>" && name("capture", "dreamcast") == "<console>", "another console, or a name this version does not know: Console");
    check(name("ps5", "") == "PS5" && name("ps5", "xbox") == "PS5" && name("ps5", "ps5pro") == "PS5 Pro", "PS5 Remote Play is a PS5 whatever is chosen, a Pro when that is chosen");
    check(name("xbox", "ps5") == "Xbox" && name("moonlight", "ps5") == "PC", "Xbox streaming is an Xbox, a PC stream a PC");
    check(name("file", "ps5") == "<original>" && name("url", "") == "<original>" && name("screen", "xbox") == "<original>" && name("frameinput", "ps5") == "<original>",
          "a video is the original, never a console");
    check(name("", "ps5") == "PS5" && name("", "") == "<console>", "nothing playing yet: as a capture card would be");
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
