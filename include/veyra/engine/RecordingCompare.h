#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS before/after comparison clips. A comparison recording is a normal
// live recording (LiveRecorder.h) in one of two layouts.
// Split (CompareLayout::Split): the picture is split down the middle:
//  - the right half is every presented frame, generated ones included (what
//    PushFPS shows, with every enhancement);
//  - the left half is the console: the last real frame, held until the next
//    real frame arrives, so it moves at the console's own frame rate.
// Side by side (CompareLayout::SideBySide): the whole frame twice at half size,
// the console on the left and every presented frame on the right, centred
// vertically on black (shaders/PushCompareSideBySide.hlsl); the labels sit in
// the black bar above each picture.
//
// The console side is the raw console picture when the presenter passes it
// (RecordingRawFrame: the graph's untouched copy of the input, kept while a
// comparison clip records): no AI enhancement, upscaling or colour work, only
// scaled to the recorded size (shaders/PushCompareRaw.hlsl). Without it (the
// first frames, or a source without references) the held enhanced real frame
// is used as before.
//
// Labels: a luma-only label (RecordingLabel::luma) is copied in; a colour label
// (RecordingLabel::rgba: rounded badges, the PushFPS logo) is blended onto the
// picture with its alpha (shaders/PushCompareOverlay.hlsl). In HDR (P010, PQ)
// white is written at 203 nits, the HDR reference white. A label too large for
// its place is scaled down to fit, never left out.
// tests/integration/PushGpuTests.cpp runs all of it on WARP.
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace veyra::pipeline { class StateTracker; }

namespace veyra::engine {

// A label. luma: full-range grey levels (255 white, 0 black), copied in as
// limited-range luma with neutral colour. rgba (preferred when present):
// straight-alpha sRGB RGBA8, blended onto the picture. Width and height are
// rounded down to even numbers when placed.
struct RecordingLabel {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> luma;
    std::vector<uint8_t> rgba;
};
// The label resampled (area average; colour weighted by alpha) to width x height.
RecordingLabel scaleRecordingLabel(const RecordingLabel& label, uint32_t width, uint32_t height);
// Called once per recording file on the engine thread with the recorded size.
using RecordingLabelRenderer = std::function<void(uint32_t width, uint32_t height, RecordingLabel& left, RecordingLabel& right)>;

// The raw console picture of a real frame: linear BT.709 in scRGB units
// (1 = 80 nits), a float RGBA texture of the source size, COMMON on entry and
// left in COMMON. sdrInHdr: an SDR source shown in HDR (RTX Video HDR), scaled
// to the 203-nit reference white as the presenter's comparison view does.
struct RecordingRawFrame {
    ID3D12Resource* texture = nullptr;
    bool sdrInHdr = false;
};

enum class CompareLayout : uint32_t { Split = 0, SideBySide = 1 };

class RecordingCompare {
public:
    RecordingCompare();
    ~RecordingCompare();
    RecordingCompare(const RecordingCompare&) = delete;
    RecordingCompare& operator=(const RecordingCompare&) = delete;
    // y: width x height (R8, or R16 for HDR); uv: half size (R8G8 or R16G16).
    bool create(ID3D12Device* device, uint32_t width, uint32_t height, bool hdr, const RecordingLabel& left,
                const RecordingLabel& right, std::wstring& reason, CompareLayout layout = CompareLayout::Split);
    // After the conversion wrote y and uv: updates the console side (from `raw`
    // on a real frame when given, else from the real frame itself), composes the
    // layout, then the divider and labels. `states` tracks y, uv and this
    // object's textures. `slot` is the recorder's frame slot: a descriptor for
    // `raw` is written there, reused only after that slot's frame finished.
    void compose(ID3D12GraphicsCommandList* list, pipeline::StateTracker& states, ID3D12Resource* y, ID3D12Resource* uv, bool generated,
                 const RecordingRawFrame* raw = nullptr, unsigned slot = 0);
    uint64_t generatedFrames() const { return generated_; }
    uint64_t realFrames() const { return real_; }
    uint64_t rawFrames() const { return rawFrames_; }
    bool rawHold() const { return rawHold_; }     // the console side shows the raw picture now
    bool rawAvailable() const { return raw_ != nullptr; }
    bool labelled() const { return !patches_.empty() || !overlays_.empty(); }
    uint32_t splitX() const { return split_; }
    CompareLayout layout() const { return layout_; }
    // Side by side: where the two half-size pictures are.
    uint32_t pictureTop() const { return top_; }
    uint32_t pictureWidth() const { return pictureW_; }
    uint32_t pictureHeight() const { return pictureH_; }
    uint32_t rightX() const { return rightX_; }

    struct Patch { uint32_t x = 0, y = 0, width = 0, height = 0; uint64_t yOffset = 0, uvOffset = 0; uint32_t yPitch = 0, uvPitch = 0; };
    // The divider and luma labels (copied in).
    const std::vector<Patch>& patches() const { return patches_; }
    struct Overlay { uint32_t x = 0, y = 0, width = 0, height = 0; uint64_t offset = 0; uint32_t pitch = 0; };
    // Colour labels (blended in).
    const std::vector<Overlay>& overlays() const { return overlays_; }

private:
    struct SideBySide; // the compute passes, kept out of this header
    struct Raw;
    struct Blend;
    void rawPass(ID3D12GraphicsCommandList* list, pipeline::StateTracker& states, const RecordingRawFrame& raw, unsigned slot);
    void blend(ID3D12GraphicsCommandList* list, pipeline::StateTracker& states, ID3D12Resource* y, ID3D12Resource* uv);
    Microsoft::WRL::ComPtr<ID3D12Resource> holdY_, holdUv_, upload_;
    std::unique_ptr<SideBySide> sideBySide_;
    std::unique_ptr<Raw> raw_;
    std::unique_ptr<Blend> blend_;
    std::vector<Patch> patches_;
    std::vector<Overlay> overlays_;
    CompareLayout layout_ = CompareLayout::Split;
    uint32_t width_ = 0, height_ = 0, split_ = 0;
    uint32_t top_ = 0, pictureW_ = 0, pictureH_ = 0, rightX_ = 0;
    bool hdr_ = false, holdValid_ = false, rawHold_ = false;
    uint64_t generated_ = 0, real_ = 0, rawFrames_ = 0;
};

} // namespace veyra::engine
