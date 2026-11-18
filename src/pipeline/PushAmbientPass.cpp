// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#include "veyra/pipeline/PushAmbientPass.h"
#include <bit>
#include <format>

namespace veyra::pipeline {
namespace {
// Between two render() calls: the small picture is readable by the blur, the blurred one by the present shader.
constexpr D3D12_RESOURCE_STATES kSmallRest = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr D3D12_RESOURCE_STATES kBlurredRest = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

ComPtr<ID3D12Resource> makeSmall(ID3D12Device* device, D3D12_RESOURCE_STATES state, const wchar_t* name) {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = PushAmbientPass::kWidth; td.Height = PushAmbientPass::kHeight; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = PushAmbientPass::kFormat; td.SampleDesc.Count = 1;
    td.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> r;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, state, nullptr, IID_PPV_ARGS(&r)))) return nullptr;
    r->SetName(name);
    return r;
}
D3D12_SHADER_RESOURCE_VIEW_DESC viewOf(DXGI_FORMAT format) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    return srv;
}
void barrierTo(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    list->ResourceBarrier(1, &b);
}
}

bool PushAmbientPass::initialize(ID3D12Device* device, unsigned sources) {
    ready_ = false;
    if (!device || !sources) return false;
    sources_ = sources;
    small_ = makeSmall(device, kSmallRest, L"PushFPS ambient fill (averaged)");
    blurred_ = makeSmall(device, kBlurredRest, L"PushFPS ambient fill (blurred)");
    if (!small_ || !blurred_) return false;
    std::vector<uint8_t> cs;
    // down: { t0 frame } per source, then { u0 small }. blur: { t0 small, u0 blurred }.
    if (!down_.loadShader("PushAmbientDown.dxil", cs) || !down_.create(device, cs, sources_ + 1, 1, 1, kConstants, 0, true)) return false;
    if (!blur_.loadShader("PushAmbientBlur.dxil", cs) || !blur_.create(device, cs, 2, 1, 1, kConstants)) return false;
    if (!stager_.initialize(device, sources_ + 4)) return false;
    makeUav(device, small_.Get(), kFormat, cpuHandleOf(down_, sources_));
    const D3D12_SHADER_RESOURCE_VIEW_DESC view = viewOf(kFormat);
    stager_.stageSrv(small_.Get(), &view, blur_.heap.Get(), 0);
    makeUav(device, blurred_.Get(), kFormat, cpuHandleOf(blur_, 1));
    ready_ = true;
    veyra::log::info("push-ambient", std::format("created {}x{} sources={}", kWidth, kHeight, sources_));
    return true;
}

void PushAmbientPass::setSource(ID3D12Device* device, unsigned index, ID3D12Resource* source, DXGI_FORMAT format) {
    if (!ready_ || !device || index >= sources_) return;
    const D3D12_SHADER_RESOURCE_VIEW_DESC view = viewOf(format);
    stager_.stageSrv(source, &view, down_.heap.Get(), index);   // a null source leaves a null view
}

void PushAmbientPass::stageResult(ID3D12DescriptorHeap* visibleHeap, UINT slot) {
    if (!ready_ || !visibleHeap) return;
    const D3D12_SHADER_RESOURCE_VIEW_DESC view = viewOf(kFormat);
    stager_.stageSrv(blurred_.Get(), &view, visibleHeap, slot);
}

bool PushAmbientPass::render(ID3D12GraphicsCommandList* list, unsigned index, ID3D12Resource* source) {
    if (!ready_ || !list || !source || index >= sources_) return false;
    float c[kConstants]{};
    c[0] = std::bit_cast<float>(kWidth); c[1] = std::bit_cast<float>(kHeight); c[2] = std::bit_cast<float>(kTaps);
    barrierTo(list, source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    barrierTo(list, small_.Get(), kSmallRest, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    down_.bind(list, c, gpuHandleOf(down_, index).ptr, gpuHandleOf(down_, sources_).ptr);
    list->Dispatch((kWidth + 7) / 8, (kHeight + 7) / 8, 1);
    barrierTo(list, small_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kSmallRest);
    barrierTo(list, source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barrierTo(list, blurred_.Get(), kBlurredRest, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    c[2] = 0.0f;
    blur_.bind(list, c, gpuHandleOf(blur_, 0).ptr, gpuHandleOf(blur_, 1).ptr);
    list->Dispatch((kWidth + 7) / 8, (kHeight + 7) / 8, 1);
    barrierTo(list, blurred_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kBlurredRest);
    return true;
}
}
