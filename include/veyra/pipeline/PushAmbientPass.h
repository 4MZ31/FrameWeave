#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS ambient fill (engine/PushAmbient.h): a frame averaged down to 96 x 54 and blurred, for
// the present shader to enlarge behind the picture. Two compute dispatches of 96 x 54 texels.
#include "veyra/pipeline/GpuPassUtils.h"

namespace veyra::pipeline {

class PushAmbientPass {
public:
    static constexpr uint32_t kWidth = 96, kHeight = 54;
    static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    static constexpr UINT kConstants = 8;
    static constexpr uint32_t kTaps = 16;   // per side, per small texel
    // sources: how many frame textures setSource will name (the presenter's output pool).
    bool initialize(ID3D12Device* device, unsigned sources);
    // The frame texture behind `index` (null: none yet).
    void setSource(ID3D12Device* device, unsigned index, ID3D12Resource* source, DXGI_FORMAT format);
    // source: the texture named for `index`, in PIXEL_SHADER_RESOURCE; it is returned in that state.
    // The list is left with this pass's descriptor heap and compute state: the caller sets its own again.
    bool render(ID3D12GraphicsCommandList* list, unsigned index, ID3D12Resource* source);
    // kWidth x kHeight, kFormat; in PIXEL_SHADER_RESOURCE between calls of render().
    ID3D12Resource* result() const { return blurred_.Get(); }
    // A view of result() in `slot` of another pass's shader-visible heap (the present pass).
    void stageResult(ID3D12DescriptorHeap* visibleHeap, UINT slot);
    bool ready() const { return ready_; }

private:
    // Views of textures go through a staging heap and are copied (GpuPassUtils.h: creating one
    // straight in a shader-visible heap broke flip-model presentation on one NVIDIA driver).
    DescriptorStager stager_;
    ComputePass down_, blur_;
    ComPtr<ID3D12Resource> small_, blurred_;
    unsigned sources_ = 0;
    bool ready_ = false;
};
}
