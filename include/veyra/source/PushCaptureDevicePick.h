#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Which video device is the capture card? (after PushFPS 4.1)
//
// Windows lists a capture card among the cameras: the laptop's webcam, an infrared camera, OBS's virtual
// camera and the card all stand in one list, in no order that means anything. Until now a PC on which no
// card had been used yet started with the first of that list chosen, which on a laptop is the webcam. The
// card was in the list, two rows down, and people wrote that PushFPS could not find their capture card
// (the lesser-known cards most of all: nothing told them apart from a camera).
//
// So the first choice is made by what the names say:
//   Card     the name says capture, HDMI, or the make or model of a capture device;
//   Unknown  it says nothing either way (many cheap cards are just "USB Video");
//   Camera   it says webcam or camera and nothing of the above;
//   Virtual  a program's camera (OBS, a phone app, PushFPS's own): never chosen by itself.
// The best kind wins, the first of that kind. This only decides what is selected when nothing was ever
// set up; a remembered card is restored as before, every device stays in the list, and the user's own
// choice is never changed.
// Pure: no device is asked here.
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>
namespace veyra::source {
enum class PushDeviceKind { Virtual = 0, Camera = 1, Unknown = 2, Card = 3 };

namespace capturepick {
inline std::wstring lower(std::wstring_view text) {
    std::wstring out; out.reserve(text.size());
    for (wchar_t c : text) out.push_back(wchar_t(std::towlower(c)));
    return out;
}
// The word as a word: "cam" is in "camera" and must not count there.
inline bool hasWord(const std::wstring& text, std::wstring_view word) {
    size_t at = 0;
    while ((at = text.find(word, at)) != std::wstring::npos) {
        const bool before = at == 0 || !std::iswalnum(text[at - 1]);
        const size_t end = at + word.size();
        const bool after = end >= text.size() || !std::iswalnum(text[end]);
        if (before && after) return true;
        ++at;
    }
    return false;
}
inline bool hasAny(const std::wstring& text, std::initializer_list<std::wstring_view> parts) {
    for (auto p : parts) if (text.find(p) != std::wstring::npos) return true;
    return false;
}
}

inline PushDeviceKind pushDeviceKind(std::wstring_view name) {
    using namespace capturepick;
    const std::wstring n = lower(name);
    // A program's camera, whatever else its name says ("OBS Virtual Camera", "NDI Webcam Video 1").
    if (hasAny(n, {L"virtual", L"droidcam", L"iriun", L"epoccam", L"manycam", L"xsplit", L"snap camera", L"streamlabs", L"nvidia broadcast",
                   L"vtube", L"animaze", L"mmhmm", L"camo", L"screen capture", L"screen-capture", L"desktop capture"}) ||
        hasWord(n, L"obs") || hasWord(n, L"ndi") || hasWord(n, L"vcam") || hasWord(n, L"splitcam"))
        return PushDeviceKind::Virtual;
    // Cameras that carry a capture maker's name.
    const bool camera = hasAny(n, {L"webcam", L"facecam", L"face cam", L"brio", L"streamcam", L"kiyo"}) || hasWord(n, L"camera") ||
        hasWord(n, L"cam") || hasWord(n, L"ir") || hasWord(n, L"facetime");
    // "Cam Link" is a capture device and has "cam" as a word.
    if (hasAny(n, {L"cam link", L"camlink"})) return PushDeviceKind::Card;
    if (!camera || hasAny(n, {L"capture", L"hdmi"})) {
        if (hasAny(n, {L"capture", L"hdmi", L"game", L"gamer", L"elgato", L"avermedia", L"magewell", L"blackmagic", L"decklink", L"intensity",
                       L"ultrastudio", L"hauppauge", L"ripsaw", L"shadowcast", L"genki", L"nzxt signal", L"signal hd", L"signal 4k", L"evga xr1",
                       L"hd60", L"4k60", L"4k x", L"hd pvr", L"mirabox", L"acasis", L"ezcap", L"pengo", L"michensen", L"kuhaimi", L"ugreen",
                       L"usb3.0 hd", L"usb3 video", L"usb 3.0 video", L"uhd video", L"fhd video", L"live streamer", L"video grabber", L"macrosilicon"}) ||
            hasWord(n, L"xr1") || hasWord(n, L"gc311") || hasWord(n, L"gc513") || hasWord(n, L"gc553") || hasWord(n, L"gc573"))
            return PushDeviceKind::Card;
    }
    return camera ? PushDeviceKind::Camera : PushDeviceKind::Unknown;
}

// The device to start with when none was ever chosen: an index into `names`, or -1 when there is none to
// choose (an empty list, or programs' cameras only).
inline int pushPickCaptureDevice(const std::vector<std::wstring>& names) {
    int best = -1; PushDeviceKind kind = PushDeviceKind::Virtual;
    for (size_t i = 0; i < names.size(); ++i) {
        const auto k = pushDeviceKind(names[i]);
        if (k == PushDeviceKind::Virtual) continue;
        if (best < 0 || int(k) > int(kind)) { best = int(i); kind = k; }
    }
    return best;
}
}
