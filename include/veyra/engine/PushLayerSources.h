#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS streaming mode sources (qml/Veyra/StudioPage.qml, bridge PushStudio.cpp): moving pictures a
// scene adds on top of the game in recordings and streams, as OBS's sources do.
//  - Camera: a webcam through Media Foundation (any format the camera offers, converted to RGB32 by
//    the source reader). Automatic takes the format closest to 1280 x 720 at 30 FPS or more; a chosen
//    resolution (720p, 1080p, 1440p, 4K) takes the closest format the camera offers, which
//    pushSourceStatus lists (a camera in webcam mode often sends less than it records).
//  - Window: another program's window (a browser with alerts or chat, a music player), drawn with
//    PrintWindow(PW_RENDERFULLCONTENT) about 15 times a second; minimised windows have no picture.
//  - Video file: a looping video (an intro, a background) through the Media Foundation source reader,
//    paced by its own timestamps. Its sound is not used.
// Each source runs on its own thread and publishes straight-alpha RGBA frames into a live layer slot
// (engine/PushStreamOverlay.h), after the cheap per-frame touches: mirror, rounded corners or a circle.
// A frame larger than the layer is shown (its share of the picture, setPushSourceReferenceWidth) is
// averaged down on that thread first, so the recorder's thread never copies more pixels than it draws.
// A source that the next scene uses too keeps running (a camera does not blink between scenes).
#include "veyra/engine/PushStreamOverlay.h"

#include <cstddef>
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
    float cropLeft = 0, cropTop = 0, cropRight = 0, cropBottom = 0;  // fractions of each side, cut first
    PushLayerPlace place;        // where (place.visible is set by setPushSources)
    uint32_t cameraHeight = 0;   // camera: 0 automatic, else the wanted picture height (720, 1080, 1440, 2160)
};

// One format a camera offers. fourcc: the subtype's four characters (MJPG, NV12, YUY2, H264, ...).
struct PushCameraFormat {
    uint32_t width = 0, height = 0;
    double fps = 0;
    uint32_t fourcc = 0;
    bool operator==(const PushCameraFormat&) const = default;
};
// The format to open, as an index into `formats` (-1 when the list is empty). wantHeight 0 is
// automatic: closest to 1280 x 720 at 30 FPS or more, never above 1920 wide (what every camera got
// before 3.2.3, unchanged). Otherwise the format closest to a 16:9 picture of that height, of any
// width, at 24 FPS or more when the camera has one.
int choosePushCameraFormat(const std::vector<PushCameraFormat>& formats, uint32_t wantHeight);
// The sizes in `formats`, largest first, each once with its highest frame rate (for the interface).
std::vector<PushCameraFormat> pushCameraSizes(const std::vector<PushCameraFormat>& formats);

struct PushCameraInfo { std::wstring id, name; };

struct PushSourceStatus {
    std::string id;
    bool running = false;        // frames arrive
    std::wstring error;          // why not (empty while it works)
    uint32_t width = 0, height = 0;
    double fps = 0;
    uint32_t captureWidth = 0, captureHeight = 0;   // what the camera or the file sends (before fitting and cropping)
    std::vector<PushCameraFormat> sizes;            // camera: the sizes it offers (pushCameraSizes)
    uint64_t serial = 0;                            // frames published so far
};

// Webcams Windows offers (enumeration only).
std::vector<PushCameraInfo> pushCameras();
// The sources of the scene on now (at most kPushLiveLayers; more are ignored). Showing and hiding
// fades over fadeMs. An empty list stops every source.
void setPushSources(const std::vector<PushSourceSpec>& sources, uint32_t fadeMs);
std::vector<PushSourceStatus> pushSourceStatus();
// The newest frame of a running source (the interface's preview), or null.
std::shared_ptr<const PushOverlayImage> pushSourceFrame(const std::string& id);
// How many frames that source has published (0: none yet): the interface redraws its live preview
// when this changes (ui/PushSourceImageProvider).
uint64_t pushSourceSerial(const std::string& id);
// The width in pixels of the picture the layers are drawn on in recordings and streams: the
// presented picture, or a larger recording or stream size (0: unknown, 1920 is assumed). The
// interface keeps it current; a layer never needs more pixels than its share of this.
void setPushSourceReferenceWidth(uint32_t width);

// Per-frame touches, platform free (tests/unit/PushStudioSourcesTests.cpp): mirror, then the shape.
// rgba: straight-alpha RGBA8, width x height; a circle crops to the centred square first. Only the
// pixels at the rounded edge are computed: until 3.2.3 every pixel of every frame took a hypot.
void shapePushSourceFrame(PushOverlayImage& image, bool mirror, PushSourceShape shape);
// RGB32 rows (B, G, R, unused; `pitch` bytes apart, negative for bottom-up) into opaque RGBA. A
// maxWidth below the width averages the picture down to it in the same pass (box filter).
void pushBgrxToRgba(const uint8_t* source, ptrdiff_t pitch, uint32_t width, uint32_t height, uint32_t maxWidth, PushOverlayImage& out);
// Averages an RGBA frame down to maxWidth wide (box filter); a frame already that narrow is left alone.
void fitPushSourceFrame(PushOverlayImage& image, uint32_t maxWidth);
// How many pixels wide a layer taking `share` of the picture's width is shown: in steps of 160 (a
// layer being resized changes its frame size rarely), at least 320, on a picture at least 1920 wide.
// 0 for no share (no limit).
uint32_t pushSourceShownWidth(float share, uint32_t referenceWidth);
// The width to fit a `width` x `height` frame to so the layer, shown `shownWidth` pixels wide (0: no
// limit), has as many pixels as it shows: a circle shows the centred square, so the frame keeps
// more. Returns `width` unless that saves at least a fifth (resampling a little only blurs).
uint32_t pushSourceFitWidth(uint32_t width, uint32_t height, uint32_t shownWidth, PushSourceShape shape);
// A scene picture made for another shape (a 16:9 card on a 9:16 stream): padded at its own
// resolution to width:height with bars, opaque black when the picture's corners are opaque (a
// card keeps covering the game), clear otherwise. False when the shapes already match (within 1 %).
bool padPushOverlayToShape(const PushOverlayImage& image, uint32_t width, uint32_t height, PushOverlayImage& out);
// Cuts fractions off each side (each at most 0.45; at least 2 x 2 pixels stay).
void cropPushSourceFrame(PushOverlayImage& image, float left, float top, float right, float bottom);

} // namespace veyra::engine
