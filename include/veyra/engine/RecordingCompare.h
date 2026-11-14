#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS before/after comparison clips. A comparison recording is a normal
// live recording (LiveRecorder.h) whose picture is split down the middle:
//  - the right half is every presented frame, generated ones included (what
//    PushFPS shows);
//  - the left half holds the last real frame, the one that came from the
//    console, until the next real frame arrives. It therefore moves at the
//    console's own frame rate, which is what the game looks like without
//    frame generation.
// A thin divider and two labels (rendered by the interface, e.g.
// "Console · 30 FPS" and "PushFPS · 60 FPS") are copied in on top.
//
// Works on the recorder's converted NV12 planes (y, uv) with copies only (no
// extra shader). SDR only for the divider and labels: an HDR recording keeps
// the split without them. tests/integration/PushGpuTests.cpp runs it on WARP.
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace veyra::pipeline { class StateTracker; }

namespace veyra::engine {

// A label as full-range grey levels (255 white text, 0 black box); width and
// height are rounded down to even numbers. It is copied into the picture as
// limited-range luma with neutral colour.
struct RecordingLabel {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> luma;
};
// Called once per recording file on the engine thread with the recorded size.
using RecordingLabelRenderer = std::function<void(uint32_t width, uint32_t height, RecordingLabel& left, RecordingLabel& right)>;

class RecordingCompare {
public:
    // y: width x height (R8, or R16 for HDR); uv: half size (R8G8 or R16G16).
    bool create(ID3D12Device* device, uint32_t width, uint32_t height, bool hdr, const RecordingLabel& left,
                const RecordingLabel& right, std::wstring& reason);
    // After the conversion wrote y and uv: keeps a real frame, puts the held
    // real frame into the left half of a generated one, then the divider and
    // labels. `states` tracks y, uv and this object's textures.
    void compose(ID3D12GraphicsCommandList* list, pipeline::StateTracker& states, ID3D12Resource* y, ID3D12Resource* uv, bool generated);
    uint64_t generatedFrames() const { return generated_; }
    uint64_t realFrames() const { return real_; }
    bool labelled() const { return !patches_.empty(); }
    uint32_t splitX() const { return split_; }

    struct Patch { uint32_t x = 0, y = 0, width = 0, height = 0; uint64_t yOffset = 0, uvOffset = 0; uint32_t yPitch = 0, uvPitch = 0; };
    const std::vector<Patch>& patches() const { return patches_; }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> holdY_, holdUv_, upload_;
    std::vector<Patch> patches_;
    uint32_t width_ = 0, height_ = 0, split_ = 0;
    bool hdr_ = false, holdValid_ = false;
    uint64_t generated_ = 0, real_ = 0;
};

} // namespace veyra::engine
