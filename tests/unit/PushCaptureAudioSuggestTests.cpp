// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Which sound input belongs to a capture card (source/PushCaptureAudioSuggest.h): chosen only where
// it is beyond doubt, never a microphone, never a guess between two. Pure: builds anywhere.
#include "veyra/source/PushCaptureAudioSuggest.h"
#include <cstdio>
#include <string>
using namespace veyra::source;
static int failures = 0;
static void check(bool ok, const std::string& label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str()); if (!ok) ++failures; }
int main() {
    const std::vector<SuggestInput> desk = {
        {L"Microphone (Realtek(R) Audio)", true}, {L"Microphone (Blue Yeti)", true}, {L"Digital Audio Interface (Elgato 4K S)", true},
        {L"Digital Audio Interface (Elgato", false}, {L"Microphone (Realtek(R) Audio)", false}, {L"PushFPS speaker", true, true}};
    check(suggestCaptureAudio(L"Elgato 4K S", false, desk) == 2, "an Elgato 4K S gets its own sound input, not a microphone");
    check(suggestCaptureAudio(L"Elgato 4K S", true, desk) == kSuggestVideoDevice, "sound found on the video device itself comes first");
    check(suggestCaptureAudio(L"Elgato 4K X", false, desk) == kSuggestNone, "another model's input is not taken for it");
    check(suggestCaptureAudio(L"USB Video", false, desk) == kSuggestNone && suggestCaptureAudio(L"USB3.0 Capture", false, desk) == kSuggestNone &&
          suggestCaptureAudio(L"HDMI Capture Device", false, desk) == kSuggestNone, "a card whose name names nothing gets no suggestion");
    check(suggestCaptureAudio(L"", false, desk) == kSuggestNone && suggestCaptureAudio(L"Elgato 4K S", false, {}) == kSuggestNone, "no name, or no inputs: none");
    {   // Two cards of one make: each gets its own.
        const std::vector<SuggestInput> two = {{L"HDMI (2- Elgato 4K X)", true}, {L"Digital Audio Interface (Elgato 4K S)", true}, {L"Microphone (USB Audio Device)", true}};
        check(suggestCaptureAudio(L"Elgato 4K X", false, two) == 0 && suggestCaptureAudio(L"Elgato 4K S", false, two) == 1, "two Elgato cards each get their own input");
        const std::vector<SuggestInput> same = {{L"Digital Audio Interface (Elgato 4K X)", true}, {L"Digital Audio Interface (2- Elgato 4K X)", true}};
        check(suggestCaptureAudio(L"Elgato 4K X", false, same) == kSuggestNone, "two inputs that both fit are a question for the user");
    }
    {   // AVerMedia and others, as Windows names them.
        const std::vector<SuggestInput> av = {{L"Microphone (Realtek(R) Audio)", true}, {L"HDMI (AVerMedia Live Gamer ULTRA 2.1-Audio)", true}, {L"AVerMedia HD Capture GC573 1", false},
                                             {L"AVerMedia HD Capture GC573 1 (AVerMedia HD Capture GC573 1)", true}};
        check(suggestCaptureAudio(L"AVerMedia Live Gamer ULTRA 2.1-Video", false, av) == 1, "a card whose video and sound names end in -Video and -Audio");
        check(suggestCaptureAudio(L"AVerMedia HD Capture GC573 1", false, av) == 3, "one input listed both ways: the newer way of opening it is taken");
        const std::vector<SuggestInput> mira = {{L"Digital Audio Interface (MiraBox Video Capture)", true}, {L"Microphone (HyperX SoloCast)", true}};
        check(suggestCaptureAudio(L"MiraBox Video Capture", false, mira) == 0, "a card named by its make alone");
        check(suggestCaptureAudio(L"HyperX Capture", false, mira) == kSuggestNone, "a microphone of the same make is never a capture card's input");
        const std::vector<SuggestInput> spoken = {{L"Microphone (Elgato 4K S)", true}, {L"Headset (Elgato 4K S)", true}};
        check(suggestCaptureAudio(L"Elgato 4K S", false, spoken) == kSuggestNone, "an input Windows calls a microphone or a headset is never chosen, even with the card's name");
    }
    {   // The virtual speaker is PushFPS's own and never a card's input.
        const std::vector<SuggestInput> v = {{L"Elgato 4K S", true, true}};
        check(suggestCaptureAudio(L"Elgato 4K S", false, v) == kSuggestNone, "the virtual speaker is never suggested");
    }
    {   // Capital letters and punctuation do not matter; a single digit Windows adds does not either.
        const std::vector<SuggestInput> p = {{L"digital audio interface (ELGATO HD60 X)", true}};
        check(suggestCaptureAudio(L"Elgato HD60 X", false, p) == 0 && suggestCaptureAudio(L"2- Elgato HD60 X", false, p) == 0, "case, punctuation and Windows' own counting are ignored");
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
