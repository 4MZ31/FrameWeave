// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// Which video device is the capture card (source/PushCaptureDevicePick.h). Builds with g++ too.
#include "veyra/source/PushCaptureDevicePick.h"
#include <cstdio>
using namespace veyra::source;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::printf("FAILED line %d: %s\n", __LINE__, #x); } } while (0)
static PushDeviceKind kind(const wchar_t* name) { return pushDeviceKind(name); }
int main() {
    using K = PushDeviceKind;
    // Names as Windows writes them for capture devices.
    for (const wchar_t* n : {L"Game Capture HD60 X", L"Elgato 4K X", L"Cam Link 4K", L"Elgato Cam Link 4K", L"Live Gamer ULTRA-Video", L"AVerMedia Live Gamer 4K",
             L"AVerMedia GC553G2", L"USB Capture HDMI 4K+", L"USB3.0 HD Video Capture", L"USB3.0 Capture", L"FHD Capture", L"Razer Ripsaw HD HDMI",
             L"ShadowCast 2", L"NZXT Signal HD60 Video", L"EVGA XR1 Lite", L"Blackmagic WDM Capture", L"Decklink Video Capture", L"Pro Capture HDMI 4K Plus",
             L"UGREEN-25173", L"Hauppauge HD PVR 60", L"HDMI to U3 capture", L"MiraBox Capture", L"ACASIS HDMI Video Capture", L"Video Grabber",
             L"UVC Camera HDMI", L"MACROSILICON USB3. 0 capture"})
        CHECK(kind(n) == K::Card);
    // Cameras, also those of a capture maker.
    for (const wchar_t* n : {L"Integrated Camera", L"Integrated Webcam", L"HD Webcam C920", L"Logitech BRIO", L"Elgato Facecam", L"Razer Kiyo Pro",
             L"USB2.0 HD UVC WebCam", L"HP Wide Vision HD Camera", L"IR Camera", L"FaceTime HD Camera", L"Logi StreamCam", L"Surface Camera Front", L"USB Cam"})
        CHECK(kind(n) == K::Camera);
    // Programs' cameras: never chosen by themselves.
    for (const wchar_t* n : {L"OBS Virtual Camera", L"PushFPS Virtual Camera", L"Veyra Virtual Camera", L"NDI Webcam Video 1", L"DroidCam Source 3",
             L"Snap Camera", L"XSplit VCam", L"NVIDIA Broadcast", L"Iriun Webcam", L"Streamlabs Desktop Virtual Webcam", L"ManyCam Virtual Webcam", L"OBS-Camera"})
        CHECK(kind(n) == K::Virtual);
    // Names that say nothing: many cheap cards.
    for (const wchar_t* n : {L"USB Video", L"USB Video Device", L"AV TO USB2.0", L"Unknown capture device", L""})
        CHECK(kind(n) == K::Unknown || kind(n) == K::Card);
    CHECK(kind(L"USB Video") == K::Unknown);
    CHECK(kind(L"Unknown capture device") == K::Card);
    // A word is a word: "observer" has no OBS in it, "Camera" no "cam", "Fire" no "ir".
    CHECK(kind(L"Observer HDMI Capture") == K::Card);
    CHECK(kind(L"Fire Capture") == K::Card);
    CHECK(kind(L"Scamp Video") == K::Unknown);

    // The first choice on a PC where no card was ever used.
    CHECK(pushPickCaptureDevice({}) == -1);
    CHECK(pushPickCaptureDevice({L"Integrated Camera", L"IR Camera", L"USB3.0 HD Video Capture"}) == 2);   // the report: the webcam was chosen
    CHECK(pushPickCaptureDevice({L"OBS Virtual Camera", L"Integrated Webcam", L"USB Video"}) == 2);          // an unnamed card before a webcam
    CHECK(pushPickCaptureDevice({L"USB Video", L"Game Capture HD60 X"}) == 1);                                 // a named card before an unnamed one
    CHECK(pushPickCaptureDevice({L"Cam Link 4K", L"Game Capture HD60 X"}) == 0);                               // two cards: the first, as before
    CHECK(pushPickCaptureDevice({L"Integrated Camera"}) == 0);                                                 // only a camera: it is still offered
    CHECK(pushPickCaptureDevice({L"OBS Virtual Camera", L"PushFPS Virtual Camera"}) == -1);                    // programs' cameras only: nothing
    CHECK(pushPickCaptureDevice({L"Elgato Facecam", L"Elgato 4K X"}) == 1);
    CHECK(pushPickCaptureDevice({L"HD Webcam C920", L"Logitech BRIO"}) == 0);
    // PushFPS's own virtual camera is not offered as a capture source; other programs' cameras stay listed.
    CHECK(pushIsOwnCamera(L"PushFPS Virtual Camera"));
    CHECK(!pushIsOwnCamera(L"OBS Virtual Camera") && !pushIsOwnCamera(L"Veyra Virtual Camera") && !pushIsOwnCamera(L"PushFPS Virtual Camera 2"));
    CHECK(kind(L"PushFPS Virtual Camera") == K::Virtual);   // and never chosen by itself: the pick's own rule
    std::printf(failures ? "%d FAILED\n" : "ALL PASSED (0 failures)\n", failures);
    return failures ? 1 : 0;
}
