#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Which sound input belongs to a capture card? (PushFPS 4.0)
//
// A capture card that was never set up starts with "do not capture sound", and the sound input
// has to be picked by hand in the capture dialog. People do not see that, connect, and report "no
// sound" (two of the first nine reports on GitHub). So the dialog now starts with the card's own
// sound input chosen, where it is beyond doubt which one that is:
//   1. the video device itself offers sound (an audio pin was found on it), or
//   2. exactly one sound input carries the card's name: every word of the card's name that says
//      something is in the input's name, and one of them is a real name (a make or a model: five
//      letters or more, not "video", "capture" and the like).
// Anything less sure is left alone: a card called "USB Video" names nothing, and two inputs that
// both fit are a question for the user. An input Windows calls a microphone or a headset is never
// chosen, whatever else its name says (a microphone of the card's make carries the make's name
// too); a card whose own input is called so has to be picked by hand, as before. The choice is
// shown in the dialog before anything is connected, and a card whose sound was set by hand, also
// to "none", keeps it.
// Pure: no device is asked here.
#include <algorithm>
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>
namespace veyra::source {
inline constexpr int kSuggestNone = -1;          // = kCaptureAudioDisabled
inline constexpr int kSuggestVideoDevice = -2;   // = kCaptureAudioFromVideoDevice
struct SuggestInput { std::wstring name; bool wasapi = false; bool virtualSpeaker = false; };

namespace capturesuggest {
inline std::vector<std::wstring> words(std::wstring_view text) {
    std::vector<std::wstring> out; std::wstring word;
    for (wchar_t c : text) {
        if (std::iswalnum(c)) word.push_back(wchar_t(std::towlower(c)));
        else if (!word.empty()) { out.push_back(std::move(word)); word.clear(); }
    }
    if (!word.empty()) out.push_back(std::move(word));
    return out;
}
// Words that say what a thing is, not which one it is.
inline bool generic(const std::wstring& w) {
    static const wchar_t* const kWords[] = {L"video", L"audio", L"capture", L"device", L"usb", L"usb2", L"usb3", L"uvc", L"hdmi", L"hd", L"uhd",
        L"digital", L"interface", L"input", L"output", L"in", L"out", L"line", L"microphone", L"mic", L"camera", L"cam", L"webcam", L"stream",
        L"streaming", L"card", L"game", L"sound", L"analog", L"wdm", L"the", L"and"};
    for (const wchar_t* k : kWords) if (w == k) return true;
    return false;
}
// What Windows calls an input a person speaks into.
inline bool spoken(const std::vector<std::wstring>& have) {
    static const wchar_t* const kWords[] = {L"microphone", L"mic", L"headset", L"headphone", L"headphones", L"webcam"};
    for (const auto& w : have) for (const wchar_t* k : kWords) if (w == k) return true;
    return false;
}
inline bool number(const std::wstring& w) { return std::all_of(w.begin(), w.end(), [](wchar_t c) { return std::iswdigit(c) != 0; }); }
// A make or a model: five letters or more, starting with a letter.
inline bool name(const std::wstring& w) { return w.size() >= 5 && std::iswalpha(w.front()) && !generic(w); }
}

// The suggestion for a card: an index into `inputs`, kSuggestVideoDevice, or kSuggestNone.
inline int suggestCaptureAudio(std::wstring_view videoName, bool videoHasAudio, const std::vector<SuggestInput>& inputs) {
    if (videoHasAudio) return kSuggestVideoDevice;
    using namespace capturesuggest;
    std::vector<std::wstring> wanted; bool named = false;
    for (auto& w : words(videoName)) {
        if (generic(w) || (number(w) && w.size() <= 1)) continue;   // "1", "2": Windows' own count of equal devices
        named = named || name(w);
        if (std::find(wanted.begin(), wanted.end(), w) == wanted.end()) wanted.push_back(std::move(w));
    }
    if (!named || wanted.empty()) return kSuggestNone;
    int only = kSuggestNone, onlyWasapi = kSuggestNone; unsigned fits = 0, fitsWasapi = 0;
    for (size_t i = 0; i < inputs.size(); ++i) {
        if (inputs[i].virtualSpeaker) continue;
        const auto have = words(inputs[i].name);
        if (spoken(have)) continue;
        const bool all = std::all_of(wanted.begin(), wanted.end(), [&](const std::wstring& w) { return std::find(have.begin(), have.end(), w) != have.end(); });
        if (!all) continue;
        ++fits; only = int(i);
        if (inputs[i].wasapi) { ++fitsWasapi; onlyWasapi = int(i); }
    }
    if (fits == 1) return only;
    // Windows lists one input twice, once for each way of opening it: the same card, so the newer way is taken.
    if (fitsWasapi == 1) return onlyWasapi;
    return kSuggestNone;
}
} // namespace veyra::source
