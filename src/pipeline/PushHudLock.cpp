// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#include "veyra/pipeline/PushHudLock.h"
#include <bit>
#include <format>

namespace veyra::pipeline {
namespace {
// x: stillness 0..1, y: held contrast, z: the same in this pair of real frames (the still rule).
constexpr DXGI_FORMAT kStateFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT kShareFormat = DXGI_FORMAT_R16_FLOAT;
constexpr uint32_t kBlock = 16;   // hudstill::kBlock
// Descriptor layouts. update: per parity { t0 current, t1 previous, u0 state }.
// compose: per (slot, held parity) { t0 generated, t1 held real, t2 state, t3 share, u0 scratch },
// and after all of those, for the still rule in place, per (slot, held parity)
// { t0 held real (never read: the generated frame is the output there), t1 held real, t2 state, t3 share, u0 generated }.
// surround: { t0 state, u0 blocks } then { t0 blocks, u0 share }.
constexpr UINT kUpdateStride = 3, kComposeStride = 5, kSurroundStride = 2;
}

std::string PushHudLock::describe() const {
    unsigned inPlace = 0;
    for (unsigned i = 0; i < generatedCount_; ++i) inPlace += inPlace_[i] ? 1u : 0u;
    return std::format("HUD lock {}x{} mode={} generated={} lock={} still={} inPlace={}", width_, height_, outputMode_, generatedCount_, lock_, still_, inPlace);
}

bool PushHudLock::initialize(ID3D12Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format, unsigned outputMode,
                             bool lock, bool still) {
    if (!device || !width || !height || format == DXGI_FORMAT_UNKNOWN || (!lock && !still)) return false;
    width_ = width; height_ = height; format_ = format; outputMode_ = outputMode; lock_ = lock; still_ = still;
    {   // Read at every start, so a test can compare the two ways.
        char value[8]{};
        const DWORD length = GetEnvironmentVariableA("VEYRA_HUD_STILL_COPY", value, sizeof(value));
        copyOnly_ = length > 0 && length < sizeof(value) && value[0] == '1';
    }
    blocksWide_ = (width_ + kBlock - 1) / kBlock; blocksHigh_ = (height_ + kBlock - 1) / kBlock;
    state_ = makeTexture(device, width_, height_, kStateFormat, true);
    scratch_ = makeTexture(device, width_, height_, format_, true);
    // Small, and always there: the compose step's fourth view must be a real texture whether or not it is read.
    blocks_ = makeTexture(device, blocksWide_, blocksHigh_, kShareFormat, true);
    share_ = makeTexture(device, blocksWide_, blocksHigh_, kShareFormat, true);
    if (!state_ || !scratch_ || !blocks_ || !share_) return false;
    std::vector<uint8_t> cs;
    if (!update_.loadShader("PushHudLockUpdate.dxil", cs) || !update_.create(device, cs, 2 * kUpdateStride, 2, 1, kConstants)) return false;
    if (!compose_.loadShader("PushHudLockCompose.dxil", cs) ||
        !compose_.create(device, cs, 2 * kMaxGenerated * 2 * kComposeStride, 4, 1, kConstants)) return false;
    if (still_ && (!surround_.loadShader("PushHudStill.dxil", cs) || !surround_.create(device, cs, 2 * kSurroundStride, 1, 1, kConstants))) {
        veyra::log::warn("hud-lock", "the still rule could not be set up; it is left out");
        still_ = false;
        if (!lock_) return false;
    }
    veyra::log::info("hud-lock", std::format("created {}", describe()));
    return true;
}

void PushHudLock::createViews(ID3D12Device* device, DescriptorStager& stager, ID3D12Resource* const real[2],
                              ID3D12Resource* const* generated, unsigned generatedCount) {
    real_[0] = real[0]; real_[1] = real[1];
    generatedCount_ = std::min<unsigned>(generatedCount, kMaxGenerated);
    for (unsigned i = 0; i < generatedCount_; ++i) generated_[i] = generated[i];
    for (unsigned parity = 0; parity < 2; ++parity) {
        const UINT base = parity * kUpdateStride;
        stager.stageSrv(real_[parity], nullptr, update_.heap.Get(), base);
        stager.stageSrv(real_[1 - parity], nullptr, update_.heap.Get(), base + 1);
        makeUav(device, state_.Get(), kStateFormat, cpuHandleOf(update_, base + 2));
    }
    for (unsigned slot = 0; slot < generatedCount_; ++slot)
        for (unsigned held = 0; held < 2; ++held) {
            const UINT base = (slot * 2 + held) * kComposeStride;
            stager.stageSrv(generated_[slot], nullptr, compose_.heap.Get(), base);
            stager.stageSrv(real_[held], nullptr, compose_.heap.Get(), base + 1);
            stager.stageSrv(state_.Get(), nullptr, compose_.heap.Get(), base + 2);
            stager.stageSrv(share_.Get(), nullptr, compose_.heap.Get(), base + 3);
            makeUav(device, scratch_.Get(), format_, cpuHandleOf(compose_, base + 4));
        }
    // The still rule alone, in place: the generated frame is the output, where its texture can be one.
    for (unsigned slot = 0; slot < kMaxGenerated; ++slot) inPlace_[slot] = false;
    if (still_ && !lock_ && !copyOnly_)
        for (unsigned slot = 0; slot < generatedCount_; ++slot) {
            if (!generated_[slot]) continue;
            const auto desc = generated_[slot]->GetDesc();
            if (!(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) || desc.Format != format_) continue;
            for (unsigned held = 0; held < 2; ++held) {
                const UINT base = (kMaxGenerated * 2 + slot * 2 + held) * kComposeStride;
                stager.stageSrv(real_[held], nullptr, compose_.heap.Get(), base);
                stager.stageSrv(real_[held], nullptr, compose_.heap.Get(), base + 1);
                stager.stageSrv(state_.Get(), nullptr, compose_.heap.Get(), base + 2);
                stager.stageSrv(share_.Get(), nullptr, compose_.heap.Get(), base + 3);
                makeUav(device, generated_[slot], format_, cpuHandleOf(compose_, base + 4));
            }
            inPlace_[slot] = true;
        }
    if (still_) {
        stager.stageSrv(state_.Get(), nullptr, surround_.heap.Get(), 0);
        makeUav(device, blocks_.Get(), kShareFormat, cpuHandleOf(surround_, 1));
        stager.stageSrv(blocks_.Get(), nullptr, surround_.heap.Get(), kSurroundStride);
        makeUav(device, share_.Get(), kShareFormat, cpuHandleOf(surround_, kSurroundStride + 1));
    }
}

bool PushHudLock::update(ID3D12GraphicsCommandList* list, StateTracker& tracker, unsigned parity, bool reset) {
    if (!list || parity > 1 || !real_[0] || !real_[1]) return false;
    float c[kConstants]{};
    c[0] = std::bit_cast<float>(width_); c[1] = std::bit_cast<float>(height_);
    c[2] = std::bit_cast<float>(uint32_t(reset ? 1 : 0)); c[3] = std::bit_cast<float>(uint32_t(outputMode_));
    c[4] = std::bit_cast<float>(uint32_t(lock_ ? 1 : 0));
    tracker.transition(list, real_[parity], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, real_[1 - parity], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, state_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const UINT base = parity * kUpdateStride;
    update_.bind(list, c, gpuHandleOf(update_, base).ptr, gpuHandleOf(update_, base + 2).ptr);
    list->Dispatch((width_ + 15) / 16, (height_ + 15) / 16, 1);
    tracker.uavBarrier(list, state_.Get());
    if (still_) {
        // How much of every pixel's surrounding stands still in this pair: per block, then over 5 x 5 blocks.
        float s[kConstants]{};
        s[0] = std::bit_cast<float>(width_); s[1] = std::bit_cast<float>(height_);
        s[3] = std::bit_cast<float>(blocksWide_); s[4] = std::bit_cast<float>(blocksHigh_);
        tracker.transition(list, state_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker.transition(list, blocks_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        s[2] = std::bit_cast<float>(uint32_t(0));
        surround_.bind(list, s, gpuHandleOf(surround_, 0).ptr, gpuHandleOf(surround_, 1).ptr);
        list->Dispatch((blocksWide_ + 7) / 8, (blocksHigh_ + 7) / 8, 1);
        tracker.uavBarrier(list, blocks_.Get());
        tracker.transition(list, blocks_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker.transition(list, share_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        s[2] = std::bit_cast<float>(uint32_t(1));
        surround_.bind(list, s, gpuHandleOf(surround_, kSurroundStride).ptr, gpuHandleOf(surround_, kSurroundStride + 1).ptr);
        list->Dispatch((blocksWide_ + 7) / 8, (blocksHigh_ + 7) / 8, 1);
        tracker.uavBarrier(list, share_.Get());
        tracker.transition(list, blocks_.Get(), D3D12_RESOURCE_STATE_COMMON);
        tracker.transition(list, share_.Get(), D3D12_RESOURCE_STATE_COMMON);
    }
    tracker.transition(list, state_.Get(), D3D12_RESOURCE_STATE_COMMON);
    tracker.transition(list, real_[parity], D3D12_RESOURCE_STATE_COMMON);
    tracker.transition(list, real_[1 - parity], D3D12_RESOURCE_STATE_COMMON);
    return true;
}

bool PushHudLock::compose(ID3D12GraphicsCommandList* list, StateTracker& tracker, unsigned slot, unsigned heldParity) {
    if (!list || slot >= generatedCount_ || heldParity > 1 || !generated_[slot] || !real_[heldParity]) return false;
    auto* generated = generated_[slot];
    float c[kConstants]{};
    c[0] = std::bit_cast<float>(width_); c[1] = std::bit_cast<float>(height_);
    c[2] = std::bit_cast<float>(uint32_t(outputMode_));
    if (inPlace_[slot]) {
        // The still rule alone: its pixels go straight into the generated frame; nothing else is touched.
        c[3] = std::bit_cast<float>(uint32_t(2u | 4u));
        tracker.transition(list, real_[heldParity], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker.transition(list, state_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker.transition(list, share_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker.transition(list, generated, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const UINT place = (kMaxGenerated * 2 + slot * 2 + heldParity) * kComposeStride;
        compose_.bind(list, c, gpuHandleOf(compose_, place).ptr, gpuHandleOf(compose_, place + 4).ptr);
        list->Dispatch((width_ + 15) / 16, (height_ + 15) / 16, 1);
        tracker.uavBarrier(list, generated);
        tracker.transition(list, generated, D3D12_RESOURCE_STATE_COMMON);
        tracker.transition(list, share_.Get(), D3D12_RESOURCE_STATE_COMMON);
        tracker.transition(list, state_.Get(), D3D12_RESOURCE_STATE_COMMON);
        tracker.transition(list, real_[heldParity], D3D12_RESOURCE_STATE_COMMON);
        return true;
    }
    c[3] = std::bit_cast<float>(uint32_t((lock_ ? 1u : 0u) | (still_ ? 2u : 0u)));
    tracker.transition(list, generated, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, real_[heldParity], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, state_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, share_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, scratch_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const UINT base = (slot * 2 + heldParity) * kComposeStride;
    compose_.bind(list, c, gpuHandleOf(compose_, base).ptr, gpuHandleOf(compose_, base + 4).ptr);
    list->Dispatch((width_ + 15) / 16, (height_ + 15) / 16, 1);
    tracker.uavBarrier(list, scratch_.Get());
    tracker.transition(list, scratch_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
    tracker.transition(list, generated, D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(generated, scratch_.Get());
    tracker.transition(list, generated, D3D12_RESOURCE_STATE_COMMON);
    tracker.transition(list, scratch_.Get(), D3D12_RESOURCE_STATE_COMMON);
    tracker.transition(list, share_.Get(), D3D12_RESOURCE_STATE_COMMON);
    tracker.transition(list, state_.Get(), D3D12_RESOURCE_STATE_COMMON);
    tracker.transition(list, real_[heldParity], D3D12_RESOURCE_STATE_COMMON);
    return true;
}
}
