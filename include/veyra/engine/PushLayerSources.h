#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS streaming mode sources (qml/Veyra/StudioPage.qml, bridge PushStudio.cpp): moving pictures a
// scene adds on top of the game in recordings and streams, as OBS's sources do.
//  - Camera: a webcam through Media Foundation (any format the camera offers, converted to RGB32 by
//    the source reader), closest to 1280 x 720 at 30 FPS or more.
//  - Window: another program's window (a browser with alerts or chat, a music player), drawn with
//    PrintWindow(PW_RENDERFULLCONTENT) about 15 times a second; minimised windows have no picture.
//  - Video file: a looping video (an intro, a background) through the Media Foundation source reader,
//    paced by its own timestamps. Its sound is not used.
// Each source runs on its own thread and publishes straight-alpha RGBA frames into a live layer slot
// (engine/PushStreamOverlay.h), after the cheap per-frame touches: mirror, rounded corners or a circle.
// A source that the next scene uses too keeps running (a camera does not blink between scenes).
#include "veyra/engine/PushStreamOverlay.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace veyra::engine {

enum class PushSourceKind { Camera, Window, Media };
enum class PushSourceShape { Rectangle = 0, Rounded = 1, Circle = 2 };

struct PushSourceSpec {
    std::string id;              // the interface's id for this source
    PushSourceKind kind = PushSourceKind::Camera;
    std::wstring target;         // camera: symbolic link; window: the HWND in decimal; video: file path
    bool mirror = false;
    bool loop = true;            // video file: start again at the end
    PushSourceShape shape = PushSourceShape::Rectangle;
    PushLayerPlace place;        // where (place.visible is set by setPushSources)
};

struct PushCameraInfo { std::wstring id, name; };

struct PushSourceStatus {
    std::string id;
    bool running = false;        // frames arrive
    std::wstring error;          // why not (empty while it works)
    uint32_t width = 0, height = 0;
    double fps = 0;
};

// Webcams Windows offers (enumeration only).
std::vector<PushCameraInfo> pushCameras();
// The sources of the scene on now (at most kPushLiveLayers; more are ignored). Showing and hiding
// fades over fadeMs. An empty list stops every source.
void setPushSources(const std::vector<PushSourceSpec>& sources, uint32_t fadeMs);
std::vector<PushSourceStatus> pushSourceStatus();
// The newest frame of a running source (the interface's preview), or null.
std::shared_ptr<const PushOverlayImage> pushSourceFrame(const std::string& id);

// Per-frame touches, platform free (tests/unit/PushLayerSourcesTests.cpp): mirror, then the shape.
// rgba: straight-alpha RGBA8, width x height; a circle crops to the centred square first.
void shapePushSourceFrame(PushOverlayImage& image, bool mirror, PushSourceShape shape);

} // namespace veyra::engine
