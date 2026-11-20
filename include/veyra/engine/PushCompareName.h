#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// What the original's side of a comparison clip is called (PushFPS 4.1; a user's request: clips
// get shared around, and "Console" does not say which one).
//
// The name is only as sure as what is known:
//   - a video (a file, a web video, a window or screen, the frame input): the original;
//   - PS5 Remote Play: a PS5 (a PS5 Pro when that is the console chosen in quick setup);
//   - Xbox streaming: an Xbox; a PC stream (Moonlight / Sunshine): a PC;
//   - a capture card: the console chosen in quick setup, but only when someone chose it. Quick
//     setup shows PlayStation 5 before anything is chosen, and a clip of an Xbox labelled "PS5"
//     because nobody touched that row would be worse than one labelled "Console".
// Pure: builds anywhere. The words for Original and Console are translated by the caller.
#include <string_view>
namespace veyra::engine {
enum class PushCompareSide { Original, Console, Named };
struct PushCompareName {
    PushCompareSide side = PushCompareSide::Console;
    const char* name = "";   // for Named: as people write it, the same in every language
};
// kind: the source's kind as the bridge names it. console: preference pushConsole ("" when never set).
constexpr PushCompareName pushCompareSourceName(std::string_view kind, std::string_view console) {
    if (kind == "file" || kind == "url" || kind == "screen" || kind == "frameinput") return {PushCompareSide::Original, ""};
    if (kind == "ps5") return {PushCompareSide::Named, console == "ps5pro" ? "PS5 Pro" : "PS5"};
    if (kind == "xbox") return {PushCompareSide::Named, "Xbox"};
    if (kind == "moonlight") return {PushCompareSide::Named, "PC"};
    if (console == "ps5") return {PushCompareSide::Named, "PS5"};
    if (console == "ps5pro") return {PushCompareSide::Named, "PS5 Pro"};
    if (console == "xbox") return {PushCompareSide::Named, "Xbox Series X|S"};
    if (console == "switch") return {PushCompareSide::Named, "Switch"};
    if (console == "switch2") return {PushCompareSide::Named, "Switch 2"};
    return {PushCompareSide::Console, ""};   // "other", never chosen, or a name this version does not know
}
} // namespace veyra::engine
