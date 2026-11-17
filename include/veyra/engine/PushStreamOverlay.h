#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS streaming mode scenes (qml/Veyra/StudioPage.qml): what recordings, the instant replay and
// live streams carry on top of the picture.
//  - a game scene with overlays (an image such as your logo, a text tag) blended onto the picture;
//  - a card scene ("Starting soon", "Be right back", "Thanks for watching"): an opaque full picture
//    that replaces the game in the output while it is on.
// The interface draws the scene as one straight-alpha sRGB RGBA picture of the output size
// (bridge PushStudio.cpp) and publishes it here; every recorder segment picks the newest one up on
// its next frame and blends it onto its NV12/P010 planes (FrameOverlay, shaders/PushCompareOverlay.hlsl,
// the comparison clip badge blend, run over the whole frame). What you see in PushFPS itself is
// unchanged, as in OBS's program output. tests/integration/PushGpuTests.cpp runs it on WARP.
#include "veyra/engine/RecordingCompare.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace veyra::pipeline { class StateTracker; }

namespace veyra::engine {

// The scene's picture: straight-alpha sRGB RGBA8, `width` x `height`. Null: no overlay.
using PushOverlayImage = RecordingLabel;
// transitionMs: how long the new picture fades in over the previous one (scene transitions,
// 0 = cut). Recorders fade on the steady clock, so every output shows the same transition.
void setPushStreamOverlay(std::shared_ptr<const PushOverlayImage> image, uint32_t transitionMs = 0);
uint32_t pushStreamTransitionMs();
// Fade position 0..1 of a transition that started `elapsedMs` ago (0 ms: already done).
inline float pushTransitionProgress(double elapsedMs, uint32_t transitionMs) {
    if (transitionMs == 0 || !(elapsedMs >= 0)) return elapsedMs >= 0 || transitionMs == 0 ? 1.0f : 0.0f;
    const double t = elapsedMs / double(transitionMs);
    return t >= 1.0 ? 1.0f : float(t * t * (3.0 - 2.0 * t)); // smoothstep: eases in and out
}
// The newest picture and its version (+1 per change; 0 before the first).
std::shared_ptr<const PushOverlayImage> pushStreamOverlay(uint64_t& version);
uint64_t pushStreamOverlayVersion();

// One recorder's copy of the scene picture, at its frame size, blended per frame.
class FrameOverlay {
public:
    FrameOverlay();
    ~FrameOverlay();
    FrameOverlay(const FrameOverlay&) = delete;
    FrameOverlay& operator=(const FrameOverlay&) = delete;
    // `image` is scaled to width x height when its size differs. False: no GPU resources (no overlay).
    bool create(ID3D12Device* device, uint32_t width, uint32_t height, bool hdr, const PushOverlayImage& image);
    // After the conversion wrote y and uv (UAV-capable R8/R16 and R8G8/R16G16 planes of the frame size).
    // opacity 0..1 scales the picture's alpha (scene transitions).
    void apply(ID3D12GraphicsCommandList* list, pipeline::StateTracker& states, ID3D12Resource* y, ID3D12Resource* uv, float opacity = 1.0f);
    bool ready() const { return ready_; }
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
    bool ready_ = false;
};

// --- live layers -----------------------------------------------------------------------------
// Moving pictures in a scene (engine/PushLayerSources.h: a webcam, a window, a video file), each in
// one of kPushLiveLayers slots: the newest frame (straight-alpha sRGB RGBA8, any size) and where it
// goes, as fractions of the output (top-left origin). Recorders blend them over the scene picture,
// so a camera stays visible on a card scene too, scaled with a box filter (LiveLayerBlend,
// shaders/PushLayerBlend.hlsl). Showing or hiding a slot fades over `fadeMs`.
inline constexpr int kPushLiveLayers = 4;
struct PushLayerPlace {
    // (x, y): the anchor point; anchor says which corner of the layer sits there (0 top left, 1 top
    // right, 2 bottom left, 3 bottom right, 4 centre). h <= 0 keeps the frame's own shape.
    float x = 0, y = 0, w = 0, h = 0;
    int anchor = 0;
    bool visible = false;
    bool operator==(const PushLayerPlace&) const = default;
};
// The place as a top-left rectangle for a frame of frameW x frameH on an outW x outH picture.
PushLayerPlace resolvePushLayerPlace(const PushLayerPlace& place, uint32_t frameW, uint32_t frameH, uint32_t outW, uint32_t outH);
// The rectangle in pixels of a `width` x `height` frame: even-aligned, clipped, at least 2 x 2.
// False when nothing of it is on the frame.
bool pushLayerRect(const PushLayerPlace& place, uint32_t width, uint32_t height,
                   uint32_t& x0, uint32_t& y0, uint32_t& w, uint32_t& h);
void setPushLiveLayerFrame(int slot, std::shared_ptr<const PushOverlayImage> frame);
void setPushLiveLayerPlace(int slot, const PushLayerPlace& place, uint32_t fadeMs);
struct PushLiveLayerState {
    std::shared_ptr<const PushOverlayImage> frame;
    uint64_t frameVersion = 0;
    PushLayerPlace place;
    float opacity = 0;   // now, following the fade
};
std::array<PushLiveLayerState, kPushLiveLayers> pushLiveLayers();
// Any slot with a frame that is visible or still fading out.
bool pushLiveLayersActive();

// One recorder's GPU side of the live layers, at its frame size.
class LiveLayerBlend {
public:
    LiveLayerBlend();
    ~LiveLayerBlend();
    LiveLayerBlend(const LiveLayerBlend&) = delete;
    LiveLayerBlend& operator=(const LiveLayerBlend&) = delete;
    // ringFrames: frames the recorder can have in flight (upload buffers are reused after that).
    bool create(ID3D12Device* device, uint32_t width, uint32_t height, bool hdr, uint32_t ringFrames);
    // After the scene picture: blends every visible slot onto y and uv.
    void apply(ID3D12GraphicsCommandList* list, pipeline::StateTracker& states, ID3D12Resource* y, ID3D12Resource* uv);
    // The same with an explicit state (tests).
    void apply(ID3D12GraphicsCommandList* list, pipeline::StateTracker& states, ID3D12Resource* y, ID3D12Resource* uv,
               const std::array<PushLiveLayerState, kPushLiveLayers>& layers);
    bool ready() const { return ready_; }
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
    bool ready_ = false;
};

} // namespace veyra::engine
