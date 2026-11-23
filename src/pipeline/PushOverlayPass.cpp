// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#include "veyra/pipeline/PushOverlayPass.h"
#include <bit>
#include <format>

namespace veyra::pipeline {
namespace {
constexpr DXGI_FORMAT kCountFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;   // four counts, or four memories, a pixel
constexpr DXGI_FORMAT kFlagFormat = DXGI_FORMAT_R16_FLOAT;             // 0 or 1
// The track shader (PushOverlay.hlsl) has five steps a real frame; each has its own set of views per parity:
// { t0, t1, t2, u0, u1 }. A view a step does not read is given a texture that is in the right state anyway
// (the mask of the frame before as a texture to read; the accepted bars or this frame's memory to write).
// The count step reads three: the frame, whether the zone moves, and which blocks do.
constexpr UINT kStages = 5, kTrackStride = 5;
// The fill shader: per parity { t0 real frame, t1 its mask, u0 the frame frame generation gets }.
constexpr UINT kFillStride = 3;
// The compose shader: per (slot, parity) { t0 zone copy, t1 older real, t2 newer real, t3 older input,
// t4 newer input, t5 older mask, t6 newer mask, u0 generated }.
constexpr UINT kComposeStride = 8;
constexpr D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr D3D12_RESOURCE_STATES kWrite = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

std::string PushOverlayPass::describe() const {
    unsigned writable = 0;
    for (unsigned i = 0; i < generatedCount_; ++i) writable += writable_[i] ? 1u : 0u;
    return std::format("crosshair hold {}x{} zone={}+{}+{} mode={} generated={} writable={}", width_, height_, zone_.size, zone_.x, zone_.y,
                       outputMode_, generatedCount_, writable);
}

bool PushOverlayPass::initialize(ID3D12Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format, unsigned outputMode) {
    if (!device || !width || !height || format == DXGI_FORMAT_UNKNOWN) return false;
    width_ = width; height_ = height; format_ = format; outputMode_ = outputMode;
    zone_ = overlay::zoneFor(width, height);
    if (!zone_.size) { veyra::log::warn("crosshair-hold", std::format("the picture {}x{} is too small for a zone", width, height)); return false; }
    rule_ = overlay::ruleFor(height);
    blocks_ = zone_.size / uint32_t(overlay::kBlock);
    for (auto& texture : input_) texture = makeTexture(device, width_, height_, format_, true);
    state_ = makeTexture(device, zone_.size, zone_.size, kCountFormat, true);
    for (auto& texture : memory_) texture = makeTexture(device, zone_.size, zone_.size, kCountFormat, true);
    accept_ = makeTexture(device, zone_.size, zone_.size, kFlagFormat, true);
    for (auto& texture : mask_) texture = makeTexture(device, zone_.size, zone_.size, kFlagFormat, true);
    blockMoves_ = makeTexture(device, blocks_, blocks_, kFlagFormat, true);
    moving_ = makeTexture(device, 1, 1, kFlagFormat, true);
    zoneCopy_ = makeTexture(device, zone_.size, zone_.size, format_, false);
    if (!input_[0] || !input_[1] || !state_ || !memory_[0] || !memory_[1] || !accept_ || !mask_[0] || !mask_[1] || !blockMoves_ || !moving_ || !zoneCopy_) return false;
    std::vector<uint8_t> cs;
    if (!track_.loadShader("PushOverlay.dxil", cs) || !track_.create(device, cs, 2 * kStages * kTrackStride, 3, 2, kConstants)) return false;
    if (!fill_.loadShader("PushOverlayFill.dxil", cs) || !fill_.create(device, cs, 2 * kFillStride, 2, 1, kConstants)) return false;
    if (!compose_.loadShader("PushOverlayCompose.dxil", cs) || !compose_.create(device, cs, kMaxGenerated * 2 * kComposeStride, 7, 1, kConstants)) return false;
    veyra::log::info("crosshair-hold", std::format("created {}", describe()));
    return true;
}

void PushOverlayPass::createViews(ID3D12Device* device, DescriptorStager& stager, ID3D12Resource* const real[2],
                                  ID3D12Resource* const* generated, unsigned generatedCount) {
    real_[0] = real[0]; real_[1] = real[1];
    generatedCount_ = std::min<unsigned>(generatedCount, kMaxGenerated);
    for (unsigned i = 0; i < generatedCount_; ++i) generated_[i] = generated[i];
    for (unsigned parity = 0; parity < 2; ++parity) {
        ID3D12Resource* const spareRead = mask_[1 - parity].Get();      // the mask of the frame before: no step of this parity writes it
        // step:                             0 blocks           1 decide           2 count        3 judge                    4 mask
        ID3D12Resource* const t0[kStages] = {real_[parity], blockMoves_.Get(), real_[parity], state_.Get(), accept_.Get()};
        ID3D12Resource* const t1[kStages] = {real_[1 - parity], spareRead, moving_.Get(), memory_[1 - parity].Get(), spareRead};
        ID3D12Resource* const t2[kStages] = {spareRead, spareRead, blockMoves_.Get(), spareRead, spareRead};
        ID3D12Resource* const u0[kStages] = {blockMoves_.Get(), moving_.Get(), state_.Get(), accept_.Get(), mask_[parity].Get()};
        ID3D12Resource* const u1[kStages] = {accept_.Get(), accept_.Get(), accept_.Get(), memory_[parity].Get(), memory_[parity].Get()};
        const DXGI_FORMAT f0[kStages] = {kFlagFormat, kFlagFormat, kCountFormat, kFlagFormat, kFlagFormat};
        const DXGI_FORMAT f1[kStages] = {kFlagFormat, kFlagFormat, kFlagFormat, kCountFormat, kCountFormat};
        for (UINT stage = 0; stage < kStages; ++stage) {
            const UINT base = (parity * kStages + stage) * kTrackStride;
            stager.stageSrv(t0[stage], nullptr, track_.heap.Get(), base);
            stager.stageSrv(t1[stage], nullptr, track_.heap.Get(), base + 1);
            stager.stageSrv(t2[stage], nullptr, track_.heap.Get(), base + 2);
            makeUav(device, u0[stage], f0[stage], cpuHandleOf(track_, base + 3));
            makeUav(device, u1[stage], f1[stage], cpuHandleOf(track_, base + 4));
        }
        const UINT fillBase = parity * kFillStride;
        stager.stageSrv(real_[parity], nullptr, fill_.heap.Get(), fillBase);
        stager.stageSrv(mask_[parity].Get(), nullptr, fill_.heap.Get(), fillBase + 1);
        makeUav(device, input_[parity].Get(), format_, cpuHandleOf(fill_, fillBase + 2));
    }
    for (unsigned slot = 0; slot < kMaxGenerated; ++slot) writable_[slot] = false;
    for (unsigned slot = 0; slot < generatedCount_; ++slot) {
        if (!generated_[slot]) continue;
        const auto desc = generated_[slot]->GetDesc();
        if (!(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) || desc.Format != format_ || desc.Width != width_ || desc.Height != height_) continue;
        for (unsigned parity = 0; parity < 2; ++parity) {
            const UINT base = (slot * 2 + parity) * kComposeStride;
            stager.stageSrv(zoneCopy_.Get(), nullptr, compose_.heap.Get(), base);
            stager.stageSrv(real_[1 - parity], nullptr, compose_.heap.Get(), base + 1);
            stager.stageSrv(real_[parity], nullptr, compose_.heap.Get(), base + 2);
            stager.stageSrv(input_[1 - parity].Get(), nullptr, compose_.heap.Get(), base + 3);
            stager.stageSrv(input_[parity].Get(), nullptr, compose_.heap.Get(), base + 4);
            stager.stageSrv(mask_[1 - parity].Get(), nullptr, compose_.heap.Get(), base + 5);
            stager.stageSrv(mask_[parity].Get(), nullptr, compose_.heap.Get(), base + 6);
            makeUav(device, generated_[slot], format_, cpuHandleOf(compose_, base + 7));
        }
        writable_[slot] = true;
    }
}

void PushOverlayPass::constants(float* c, unsigned stage, bool reset) const {
    const uint32_t values[kConstants] = {zone_.x, zone_.y, zone_.size, stage, outputMode_, reset ? 1u : 0u, uint32_t(rule_.flank), uint32_t(rule_.stand),
                                         uint32_t(rule_.longest), uint32_t(rule_.shortest), uint32_t(rule_.fresh), uint32_t(rule_.memory),
                                         uint32_t(rule_.grow), uint32_t(rule_.reach), blocks_, uint32_t(rule_.hole)};
    for (UINT i = 0; i < kConstants; ++i) c[i] = std::bit_cast<float>(values[i]);
}

bool PushOverlayPass::update(ID3D12GraphicsCommandList* list, StateTracker& tracker, unsigned parity, bool reset) {
    if (!list || parity > 1 || !real_[0] || !real_[1] || !zone_.size) return false;
    // Nothing this pass keeps is read before it was written: the first frame starts everything over.
    if (!primed_) { reset = true; primed_ = true; }
    auto* now = real_[parity]; auto* before = real_[1 - parity];
    auto* memoryBefore = memory_[1 - parity].Get(); auto* memoryNow = memory_[parity].Get();
    auto* maskBefore = mask_[1 - parity].Get(); auto* maskNow = mask_[parity].Get();
    float c[kConstants]{};
    const auto run = [&](unsigned stage, UINT groupsX, UINT groupsY, ID3D12Resource* written) {
        constants(c, stage, reset);
        const UINT base = (parity * kStages + stage) * kTrackStride;
        track_.bind(list, c, gpuHandleOf(track_, base).ptr, gpuHandleOf(track_, base + 3).ptr);
        list->Dispatch(groupsX, groupsY, 1);
        tracker.uavBarrier(list, written);
    };
    const UINT zoneGroups = (zone_.size + 7) / 8, blockGroups = (blocks_ + 7) / 8;
    // The frame frame generation gets starts as a copy of the real frame.
    tracker.transition(list, now, D3D12_RESOURCE_STATE_COPY_SOURCE);
    tracker.transition(list, input_[parity].Get(), D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(input_[parity].Get(), now);
    tracker.transition(list, now, kRead);
    tracker.transition(list, before, kRead);
    tracker.transition(list, maskBefore, kRead);
    tracker.transition(list, memoryBefore, kRead);
    tracker.transition(list, accept_.Get(), kWrite);
    tracker.transition(list, memoryNow, kWrite);
    // 0: which blocks move
    tracker.transition(list, blockMoves_.Get(), kWrite);
    run(0, blockGroups, blockGroups, blockMoves_.Get());
    // 1: does the zone move
    tracker.transition(list, blockMoves_.Get(), kRead);
    tracker.transition(list, moving_.Get(), kWrite);
    run(1, 1, 1, moving_.Get());
    // 2: the counts
    tracker.transition(list, moving_.Get(), kRead);
    tracker.transition(list, state_.Get(), kWrite);
    run(2, zoneGroups, zoneGroups, state_.Get());
    // 3: found and followed bars; what is remembered
    tracker.transition(list, state_.Get(), kRead);
    run(3, zoneGroups, zoneGroups, accept_.Get());
    tracker.uavBarrier(list, memoryNow);
    // 4: the mask
    tracker.transition(list, accept_.Get(), kRead);
    tracker.transition(list, maskNow, kWrite);
    run(4, zoneGroups, zoneGroups, maskNow);
    // the masked pixels of the frame frame generation gets, filled in
    tracker.transition(list, maskNow, kRead);
    tracker.transition(list, input_[parity].Get(), kWrite);
    float f[kConstants]{};
    f[0] = std::bit_cast<float>(zone_.x); f[1] = std::bit_cast<float>(zone_.y); f[2] = std::bit_cast<float>(zone_.size); f[3] = std::bit_cast<float>(uint32_t(rule_.reach));
    fill_.bind(list, f, gpuHandleOf(fill_, parity * kFillStride).ptr, gpuHandleOf(fill_, parity * kFillStride + 2).ptr);
    list->Dispatch(zoneGroups, zoneGroups, 1);
    tracker.uavBarrier(list, input_[parity].Get());
    for (ID3D12Resource* r : {input_[parity].Get(), maskNow, maskBefore, memoryNow, memoryBefore, accept_.Get(), state_.Get(), moving_.Get(), blockMoves_.Get(), now, before})
        tracker.transition(list, r, D3D12_RESOURCE_STATE_COMMON);
    return true;
}

bool PushOverlayPass::compose(ID3D12GraphicsCommandList* list, StateTracker& tracker, unsigned slot, unsigned parity, float t) {
    if (!list || slot >= generatedCount_ || parity > 1 || !writable_[slot] || !real_[0] || !real_[1] || !zone_.size) return false;
    auto* generated = generated_[slot];
    // The zone of the generated frame as it was made: the frame itself is written in place.
    tracker.transition(list, generated, D3D12_RESOURCE_STATE_COPY_SOURCE);
    tracker.transition(list, zoneCopy_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION target{}, source{};
    target.pResource = zoneCopy_.Get(); target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; target.SubresourceIndex = 0;
    source.pResource = generated; source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; source.SubresourceIndex = 0;
    const D3D12_BOX box{zone_.x, zone_.y, 0, zone_.x + zone_.size, zone_.y + zone_.size, 1};
    list->CopyTextureRegion(&target, 0, 0, 0, &source, &box);
    ID3D12Resource* const reads[] = {zoneCopy_.Get(), real_[0], real_[1], input_[0].Get(), input_[1].Get(), mask_[0].Get(), mask_[1].Get()};
    for (ID3D12Resource* r : reads) tracker.transition(list, r, kRead);
    tracker.transition(list, generated, kWrite);
    float c[kConstants]{};
    c[0] = std::bit_cast<float>(zone_.x); c[1] = std::bit_cast<float>(zone_.y); c[2] = std::bit_cast<float>(zone_.size);
    c[4] = std::clamp(t, 0.0f, 1.0f);
    const UINT base = (slot * 2 + parity) * kComposeStride;
    compose_.bind(list, c, gpuHandleOf(compose_, base).ptr, gpuHandleOf(compose_, base + 7).ptr);
    list->Dispatch((zone_.size + 7) / 8, (zone_.size + 7) / 8, 1);
    tracker.uavBarrier(list, generated);
    tracker.transition(list, generated, D3D12_RESOURCE_STATE_COMMON);
    for (ID3D12Resource* r : reads) tracker.transition(list, r, D3D12_RESOURCE_STATE_COMMON);
    return true;
}
}
