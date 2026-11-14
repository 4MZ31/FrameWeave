// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#include "veyra/engine/RecordingCompare.h"
#include "veyra/pipeline/GpuPassUtils.h"

#include <algorithm>
#include <cstring>

namespace veyra::engine {
namespace {
constexpr uint32_t kPitchAlign = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;          // 256
constexpr uint64_t kPlacementAlign = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;  // 512
uint32_t alignPitch(uint32_t bytes) { return (bytes + kPitchAlign - 1) / kPitchAlign * kPitchAlign; }
uint64_t alignOffset(uint64_t at) { return (at + kPlacementAlign - 1) / kPlacementAlign * kPlacementAlign; }
uint8_t limited(uint8_t full) { return uint8_t(16 + (uint32_t(full) * 219 + 127) / 255); }
} // namespace

bool RecordingCompare::create(ID3D12Device* device, uint32_t width, uint32_t height, bool hdr, const RecordingLabel& left,
                              const RecordingLabel& right, std::wstring& reason) {
    width_ = width; height_ = height; hdr_ = hdr;
    split_ = (width / 2) & ~1u;
    holdValid_ = false; generated_ = real_ = 0; patches_.clear(); upload_.Reset();
    holdY_ = pipeline::makeTexture(device, width, height, hdr ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM, true);
    holdUv_ = pipeline::makeTexture(device, width / 2, height / 2, hdr ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM, true); // same flags as y and uv (CopyResource)
    if (!holdY_ || !holdUv_) { reason = L"录制显存分配失败"; return false; }
    if (hdr) return true; // split only

    // Divider (4 pixels, white) and the two labels.
    struct Source { uint32_t x, y, w, h; const RecordingLabel* label; };
    std::vector<Source> sources;
    const uint32_t divider = 4;
    sources.push_back({split_ - divider / 2, 0, divider, height & ~1u, nullptr});
    const uint32_t margin = std::max<uint32_t>(8, height / 36) & ~1u;
    const auto fits = [&](const RecordingLabel& l) {
        const uint32_t w = l.width & ~1u, h = l.height & ~1u;
        return w >= 2 && h >= 2 && l.luma.size() >= size_t(l.width) * l.height && w + 2 * margin + divider <= split_ && h + 2 * margin <= height / 3;
    };
    if (fits(left)) sources.push_back({margin, margin, left.width & ~1u, left.height & ~1u, &left});
    if (fits(right)) sources.push_back({(width - margin - (right.width & ~1u)) & ~1u, margin, right.width & ~1u, right.height & ~1u, &right});

    uint64_t at = 0;
    for (const auto& s : sources) {
        Patch p{s.x, s.y, s.w, s.h};
        p.yPitch = alignPitch(s.w); p.yOffset = alignOffset(at); at = p.yOffset + uint64_t(p.yPitch) * s.h;
        p.uvPitch = alignPitch(s.w); p.uvOffset = alignOffset(at); at = p.uvOffset + uint64_t(p.uvPitch) * (s.h / 2); // R8G8: w/2 * 2 bytes
        patches_.push_back(p);
    }
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = std::max<uint64_t>(at, 256); bd.Height = 1;
    bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload_)))) {
        patches_.clear(); return true; // the split still works without the overlay
    }
    uint8_t* mapped = nullptr;
    const D3D12_RANGE none{0, 0};
    if (FAILED(upload_->Map(0, &none, reinterpret_cast<void**>(&mapped))) || !mapped) { patches_.clear(); upload_.Reset(); return true; }
    for (size_t i = 0; i < sources.size(); ++i) {
        const auto& s = sources[i];
        const auto& p = patches_[i];
        for (uint32_t row = 0; row < s.h; ++row) {
            uint8_t* out = mapped + p.yOffset + uint64_t(p.yPitch) * row;
            if (!s.label) std::memset(out, 235, s.w);
            else for (uint32_t x = 0; x < s.w; ++x) out[x] = limited(s.label->luma[size_t(row) * s.label->width + x]);
        }
        for (uint32_t row = 0; row < s.h / 2; ++row) std::memset(mapped + p.uvOffset + uint64_t(p.uvPitch) * row, 128, s.w);
    }
    upload_->Unmap(0, nullptr);
    return true;
}

void RecordingCompare::compose(ID3D12GraphicsCommandList* list, pipeline::StateTracker& states, ID3D12Resource* y, ID3D12Resource* uv, bool generated) {
    if (!list || !y || !uv || !holdY_) return;
    if (!generated) {
        ++real_;
        states.transition(list, y, D3D12_RESOURCE_STATE_COPY_SOURCE);
        states.transition(list, uv, D3D12_RESOURCE_STATE_COPY_SOURCE);
        states.transition(list, holdY_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        states.transition(list, holdUv_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(holdY_.Get(), y);
        list->CopyResource(holdUv_.Get(), uv);
        holdValid_ = true;
    } else {
        ++generated_;
        if (holdValid_ && split_) {
            states.transition(list, y, D3D12_RESOURCE_STATE_COPY_DEST);
            states.transition(list, uv, D3D12_RESOURCE_STATE_COPY_DEST);
            states.transition(list, holdY_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
            states.transition(list, holdUv_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION from{}, to{};
            from.Type = to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            from.pResource = holdY_.Get(); to.pResource = y;
            const D3D12_BOX yBox{0, 0, 0, split_, height_, 1};
            list->CopyTextureRegion(&to, 0, 0, 0, &from, &yBox);
            from.pResource = holdUv_.Get(); to.pResource = uv;
            const D3D12_BOX uvBox{0, 0, 0, split_ / 2, height_ / 2, 1};
            list->CopyTextureRegion(&to, 0, 0, 0, &from, &uvBox);
        }
    }
    if (patches_.empty() || !upload_) return;
    states.transition(list, y, D3D12_RESOURCE_STATE_COPY_DEST);
    states.transition(list, uv, D3D12_RESOURCE_STATE_COPY_DEST);
    for (const auto& p : patches_) {
        D3D12_TEXTURE_COPY_LOCATION from{}, to{};
        from.pResource = upload_.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; to.SubresourceIndex = 0;
        from.PlacedFootprint.Offset = p.yOffset;
        from.PlacedFootprint.Footprint = {DXGI_FORMAT_R8_UNORM, p.width, p.height, 1, p.yPitch};
        to.pResource = y;
        list->CopyTextureRegion(&to, p.x, p.y, 0, &from, nullptr);
        from.PlacedFootprint.Offset = p.uvOffset;
        from.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8_UNORM, p.width / 2, p.height / 2, 1, p.uvPitch};
        to.pResource = uv;
        list->CopyTextureRegion(&to, p.x / 2, p.y / 2, 0, &from, nullptr);
    }
}

} // namespace veyra::engine
