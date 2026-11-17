// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#include "veyra/engine/PushStreamOverlay.h"
#include "veyra/pipeline/GpuPassUtils.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>

namespace veyra::engine {
namespace {
std::mutex overlayMutex;
std::shared_ptr<const PushOverlayImage> overlayImage;
std::atomic<uint64_t> overlayVersion{0};
std::atomic<uint32_t> overlayTransitionMs{0};
constexpr uint32_t kPitchAlign = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
} // namespace

void setPushStreamOverlay(std::shared_ptr<const PushOverlayImage> image, uint32_t transitionMs) {
    {
        std::lock_guard lock(overlayMutex);
        overlayImage = std::move(image);
        overlayTransitionMs.store(std::min<uint32_t>(transitionMs, 5000));
    }
    overlayVersion.fetch_add(1);
}
uint32_t pushStreamTransitionMs() { return overlayTransitionMs.load(); }
std::shared_ptr<const PushOverlayImage> pushStreamOverlay(uint64_t& version) {
    std::lock_guard lock(overlayMutex);
    version = overlayVersion.load();
    return overlayImage;
}
uint64_t pushStreamOverlayVersion() { return overlayVersion.load(); }

// Heap slots 0-2: the overlay, the picture's y and uv copies; 3-4: the recorder's y and uv.
struct FrameOverlay::Impl {
    ID3D12Device* device = nullptr;
    uint32_t width = 0, height = 0;
    bool hdr = false, uploaded = false;
    pipeline::ComputePass pass;
    pipeline::DescriptorStager stager;
    Microsoft::WRL::ComPtr<ID3D12Resource> overlay, bgY, bgUv, upload;
    uint32_t pitch = 0;
    ID3D12Resource* boundY = nullptr;
    ID3D12Resource* boundUv = nullptr;
};

FrameOverlay::FrameOverlay() = default;
FrameOverlay::~FrameOverlay() = default;

bool FrameOverlay::create(ID3D12Device* device, uint32_t width, uint32_t height, bool hdr, const PushOverlayImage& image) {
    ready_ = false;
    p_ = std::make_unique<Impl>();
    auto& p = *p_;
    p.device = device; p.width = width; p.height = height; p.hdr = hdr;
    if (!device || width < 2 || height < 2 || image.width < 2 || image.height < 2 ||
        image.rgba.size() < size_t(image.width) * image.height * 4) return false;
    RecordingLabel scaled;
    const RecordingLabel* src = &image;
    if (image.width != width || image.height != height) {
        scaled = scaleRecordingLabel(image, width, height);
        if (scaled.rgba.size() < size_t(width) * height * 4) return false;
        src = &scaled;
    }
    const DXGI_FORMAT yFormat = hdr ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
    const DXGI_FORMAT uvFormat = hdr ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
    p.overlay = pipeline::makeTexture(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, false);
    p.bgY = pipeline::makeTexture(device, width, height, yFormat, false);
    p.bgUv = pipeline::makeTexture(device, width / 2, height / 2, uvFormat, false);
    if (!p.overlay || !p.bgY || !p.bgUv) return false;
    std::vector<uint8_t> shader;
    if (!p.pass.loadShader("PushCompareOverlay.dxil", shader) || !p.pass.create(device, shader, 5, 3, 2) || !p.stager.initialize(device, 3))
        return false;
    p.pitch = (width * 4 + kPitchAlign - 1) / kPitchAlign * kPitchAlign;
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = uint64_t(p.pitch) * height; bd.Height = 1;
    bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&p.upload))))
        return false;
    uint8_t* mapped = nullptr;
    const D3D12_RANGE none{0, 0};
    if (FAILED(p.upload->Map(0, &none, reinterpret_cast<void**>(&mapped))) || !mapped) return false;
    for (uint32_t row = 0; row < height; ++row)
        std::memcpy(mapped + uint64_t(p.pitch) * row, &src->rgba[size_t(row) * width * 4], size_t(width) * 4);
    p.upload->Unmap(0, nullptr);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1;
    view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    p.stager.stageSrv(p.overlay.Get(), &view, p.pass.heap.Get(), 0);
    view.Format = yFormat;
    p.stager.stageSrv(p.bgY.Get(), &view, p.pass.heap.Get(), 1);
    view.Format = uvFormat;
    p.stager.stageSrv(p.bgUv.Get(), &view, p.pass.heap.Get(), 2);
    ready_ = true;
    return true;
}

void FrameOverlay::apply(ID3D12GraphicsCommandList* list, pipeline::StateTracker& states, ID3D12Resource* y, ID3D12Resource* uv, float opacity) {
    if (!ready_ || !list || !y || !uv || !(opacity > 0.001f)) return;
    auto& p = *p_;
    if (!p.uploaded) {
        states.transition(list, p.overlay.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION from{}, to{};
        from.pResource = p.upload.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        from.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, p.width, p.height, 1, p.pitch};
        to.pResource = p.overlay.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        states.transition(list, p.overlay.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        p.uploaded = true;
    }
    if (p.boundY != y || p.boundUv != uv) {
        pipeline::makeUav(p.device, y, p.hdr ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM, pipeline::cpuHandleOf(p.pass, 3));
        pipeline::makeUav(p.device, uv, p.hdr ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM, pipeline::cpuHandleOf(p.pass, 4));
        p.boundY = y; p.boundUv = uv;
    }
    // The picture under the overlay, read by the blend (a UAV is never read here).
    states.transition(list, y, D3D12_RESOURCE_STATE_COPY_SOURCE);
    states.transition(list, uv, D3D12_RESOURCE_STATE_COPY_SOURCE);
    states.transition(list, p.bgY.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
    states.transition(list, p.bgUv.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.Type = to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.pResource = y; to.pResource = p.bgY.Get();
    const D3D12_BOX yBox{0, 0, 0, p.width, p.height, 1};
    list->CopyTextureRegion(&to, 0, 0, 0, &from, &yBox);
    from.pResource = uv; to.pResource = p.bgUv.Get();
    const D3D12_BOX uvBox{0, 0, 0, p.width / 2, p.height / 2, 1};
    list->CopyTextureRegion(&to, 0, 0, 0, &from, &uvBox);
    states.transition(list, p.bgY.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    states.transition(list, p.bgUv.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    states.transition(list, y, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    states.transition(list, uv, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const float fade = 1.0f - std::clamp(opacity, 0.0f, 1.0f);
    const uint32_t constants[8] = {0, 0, p.width & ~1u, p.height & ~1u, p.hdr ? 1u : 0u, std::bit_cast<uint32_t>(fade), 0, 0};
    float bits[8];
    std::memcpy(bits, constants, sizeof(bits));
    p.pass.bind(list, bits, pipeline::gpuHandleOf(p.pass, 0).ptr, pipeline::gpuHandleOf(p.pass, 3).ptr);
    list->Dispatch((p.width + 15) / 16, (p.height + 15) / 16, 1);
    states.uavBarrier(list, y);
    states.uavBarrier(list, uv);
}

// --- live layers -----------------------------------------------------------------------------
namespace {
using LayerClock = std::chrono::steady_clock;
struct LayerSlot {
    std::shared_ptr<const PushOverlayImage> frame;
    uint64_t frameVersion = 0;
    PushLayerPlace place;
    float from = 0, to = 0;          // the fade: opacity at its start and at its end
    LayerClock::time_point fadeStart{};
    uint32_t fadeMs = 0;
    float opacityAt(LayerClock::time_point now) const {
        const double ms = std::chrono::duration<double, std::milli>(now - fadeStart).count();
        const float t = pushTransitionProgress(ms, fadeMs);
        return from + (to - from) * t;
    }
};
std::mutex layerMutex;
std::array<LayerSlot, kPushLiveLayers> layerSlots;
} // namespace

PushLayerPlace resolvePushLayerPlace(const PushLayerPlace& place, uint32_t frameW, uint32_t frameH, uint32_t outW, uint32_t outH) {
    PushLayerPlace r = place;
    if (!(r.h > 0) && frameW > 0 && frameH > 0 && outW > 0 && outH > 0)
        r.h = r.w * float(frameH) / float(frameW) * float(outW) / float(outH);
    const bool right = r.anchor == 1 || r.anchor == 3, bottom = r.anchor == 2 || r.anchor == 3;
    if (r.anchor == 4) { r.x -= r.w / 2; r.y -= r.h / 2; }
    else { if (right) r.x -= r.w; if (bottom) r.y -= r.h; }
    r.anchor = 0;
    return r;
}

bool pushLayerRect(const PushLayerPlace& place, uint32_t width, uint32_t height,
                   uint32_t& x0, uint32_t& y0, uint32_t& w, uint32_t& h) {
    if (width < 2 || height < 2 || !std::isfinite(place.x) || !std::isfinite(place.y) || !std::isfinite(place.w) ||
        !std::isfinite(place.h) || place.w <= 0 || place.h <= 0) return false;
    const double l = std::clamp(double(place.x), 0.0, 1.0) * width, t = std::clamp(double(place.y), 0.0, 1.0) * height;
    const double r = std::clamp(double(place.x + place.w), 0.0, 1.0) * width, b = std::clamp(double(place.y + place.h), 0.0, 1.0) * height;
    const uint32_t left = uint32_t(l) & ~1u, top = uint32_t(t) & ~1u;
    uint32_t right = std::min(uint32_t(std::lround(r)) & ~1u, width & ~1u), bottom = std::min(uint32_t(std::lround(b)) & ~1u, height & ~1u);
    if (right < left + 2 || bottom < top + 2) return false;
    x0 = left; y0 = top; w = right - left; h = bottom - top;
    return true;
}

void setPushLiveLayerFrame(int slot, std::shared_ptr<const PushOverlayImage> frame) {
    if (slot < 0 || slot >= kPushLiveLayers) return;
    std::lock_guard lock(layerMutex);
    auto& s = layerSlots[size_t(slot)];
    s.frame = std::move(frame);
    ++s.frameVersion;
}

void setPushLiveLayerPlace(int slot, const PushLayerPlace& place, uint32_t fadeMs) {
    if (slot < 0 || slot >= kPushLiveLayers) return;
    std::lock_guard lock(layerMutex);
    auto& s = layerSlots[size_t(slot)];
    const auto now = LayerClock::now();
    const float target = place.visible ? 1.0f : 0.0f;
    if (target != s.to) {
        s.from = s.opacityAt(now); s.to = target; s.fadeStart = now; s.fadeMs = std::min<uint32_t>(fadeMs, 5000);
    }
    // A hidden layer keeps its last place while it fades out.
    if (place.visible || !s.place.visible) s.place = place;
    else s.place.visible = false;
}

std::array<PushLiveLayerState, kPushLiveLayers> pushLiveLayers() {
    std::array<PushLiveLayerState, kPushLiveLayers> out{};
    std::lock_guard lock(layerMutex);
    const auto now = LayerClock::now();
    for (size_t i = 0; i < out.size(); ++i) {
        const auto& s = layerSlots[i];
        out[i].frame = s.frame; out[i].frameVersion = s.frameVersion; out[i].place = s.place;
        out[i].opacity = std::clamp(s.opacityAt(now), 0.0f, 1.0f);
    }
    return out;
}

bool pushLiveLayersActive() {
    std::lock_guard lock(layerMutex);
    const auto now = LayerClock::now();
    for (const auto& s : layerSlots)
        if (s.frame && s.opacityAt(now) > 0.001f) return true;
    return false;
}

// Heap: per slot two descriptor sets (a texture recreated after a size change takes the other set)
// of three SRVs [layer, bgY, bgUv]; then the recorder's y and uv UAVs.
struct LiveLayerBlend::Impl {
    static constexpr UINT kSets = 2;
    static constexpr UINT kUav = kPushLiveLayers * kSets * 3;
    ID3D12Device* device = nullptr;
    uint32_t width = 0, height = 0, ring = 2;
    bool hdr = false;
    pipeline::ComputePass pass;
    pipeline::DescriptorStager stager;
    Microsoft::WRL::ComPtr<ID3D12Resource> bgY, bgUv;
    ID3D12Resource* boundY = nullptr;
    ID3D12Resource* boundUv = nullptr;
    uint64_t frames = 0;
    struct Slot {
        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> uploads;
        uint32_t texW = 0, texH = 0, pitch = 0, set = 0;
        uint64_t uploadedVersion = 0, createdAt = 0, uploadCount = 0;
        bool hasTexture = false;
    };
    std::array<Slot, kPushLiveLayers> slots;
    std::vector<std::pair<Microsoft::WRL::ComPtr<ID3D12Resource>, uint64_t>> retired;
};

LiveLayerBlend::LiveLayerBlend() = default;
LiveLayerBlend::~LiveLayerBlend() = default;

bool LiveLayerBlend::create(ID3D12Device* device, uint32_t width, uint32_t height, bool hdr, uint32_t ringFrames) {
    ready_ = false;
    p_ = std::make_unique<Impl>();
    auto& p = *p_;
    p.device = device; p.width = width & ~1u; p.height = height & ~1u; p.hdr = hdr; p.ring = std::max(2u, ringFrames);
    if (!device || p.width < 2 || p.height < 2) return false;
    const DXGI_FORMAT yFormat = hdr ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
    const DXGI_FORMAT uvFormat = hdr ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
    p.bgY = pipeline::makeTexture(device, p.width, p.height, yFormat, false);
    p.bgUv = pipeline::makeTexture(device, p.width / 2, p.height / 2, uvFormat, false);
    if (!p.bgY || !p.bgUv) return false;
    std::vector<uint8_t> shader;
    if (!p.pass.loadShader("PushLayerBlend.dxil", shader) || !p.pass.create(device, shader, Impl::kUav + 2, 3, 2) ||
        !p.stager.initialize(device, 8)) return false;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1;
    for (UINT set = 0; set < UINT(kPushLiveLayers) * Impl::kSets; ++set) {
        view.Format = yFormat;
        p.stager.stageSrv(p.bgY.Get(), &view, p.pass.heap.Get(), set * 3 + 1);
        view.Format = uvFormat;
        p.stager.stageSrv(p.bgUv.Get(), &view, p.pass.heap.Get(), set * 3 + 2);
    }
    ready_ = true;
    return true;
}

void LiveLayerBlend::apply(ID3D12GraphicsCommandList* list, pipeline::StateTracker& states, ID3D12Resource* y, ID3D12Resource* uv) {
    apply(list, states, y, uv, pushLiveLayers());
}

void LiveLayerBlend::apply(ID3D12GraphicsCommandList* list, pipeline::StateTracker& states, ID3D12Resource* y, ID3D12Resource* uv,
                           const std::array<PushLiveLayerState, kPushLiveLayers>& layers) {
    if (!ready_ || !list || !y || !uv) return;
    auto& p = *p_;
    ++p.frames;
    std::erase_if(p.retired, [&](const auto& r) { return p.frames > r.second + 2ull * p.ring; });
    const DXGI_FORMAT yFormat = p.hdr ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
    const DXGI_FORMAT uvFormat = p.hdr ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
    bool bound = false;
    for (size_t i = 0; i < layers.size(); ++i) {
        const auto& layer = layers[i];
        auto& slot = p.slots[i];
        const auto& frame = layer.frame;
        if (!frame || frame->width < 2 || frame->height < 2 || frame->rgba.size() < size_t(frame->width) * frame->height * 4 ||
            !(layer.opacity > 0.001f)) continue;
        uint32_t x0 = 0, y0 = 0, w = 0, h = 0;
        if (!pushLayerRect(resolvePushLayerPlace(layer.place, frame->width, frame->height, p.width, p.height), p.width, p.height, x0, y0, w, h)) continue;
        // A new frame size needs a new texture. Its descriptors go to the slot's other set, so the
        // set a frame still in flight reads is never rewritten; a second size change within that
        // window waits (the slot is skipped meanwhile).
        if (!slot.hasTexture || slot.texW != frame->width || slot.texH != frame->height) {
            if (slot.hasTexture && p.frames < slot.createdAt + 2ull * p.ring) continue;
            auto texture = pipeline::makeTexture(p.device, frame->width, frame->height, DXGI_FORMAT_R8G8B8A8_UNORM, false);
            if (!texture) continue;
            if (slot.texture) p.retired.emplace_back(slot.texture, p.frames);
            for (auto& u : slot.uploads) if (u) p.retired.emplace_back(u, p.frames);
            slot.uploads.assign(p.ring + 1, nullptr);
            slot.texture = texture;
            slot.texW = frame->width; slot.texH = frame->height;
            slot.pitch = (frame->width * 4 + kPitchAlign - 1) / kPitchAlign * kPitchAlign;
            slot.set = slot.hasTexture ? 1 - slot.set : 0;
            slot.hasTexture = true; slot.createdAt = p.frames; slot.uploadedVersion = 0;
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Texture2D.MipLevels = 1;
            view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            p.stager.stageSrv(slot.texture.Get(), &view, p.pass.heap.Get(), UINT((i * Impl::kSets + slot.set) * 3));
        }
        if (slot.uploadedVersion != layer.frameVersion) {
            // A ring of upload buffers: the CPU never writes one a frame in flight still copies from.
            auto& upload = slot.uploads[size_t(slot.uploadCount++ % slot.uploads.size())];
            if (!upload) {
                D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
                D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = uint64_t(slot.pitch) * slot.texH; bd.Height = 1;
                bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                if (FAILED(p.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))))
                    continue;
            }
            uint8_t* mapped = nullptr;
            const D3D12_RANGE none{0, 0};
            if (FAILED(upload->Map(0, &none, reinterpret_cast<void**>(&mapped))) || !mapped) continue;
            for (uint32_t row = 0; row < slot.texH; ++row)
                std::memcpy(mapped + uint64_t(slot.pitch) * row, &frame->rgba[size_t(row) * slot.texW * 4], size_t(slot.texW) * 4);
            upload->Unmap(0, nullptr);
            states.transition(list, slot.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION from{}, to{};
            from.pResource = upload.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            from.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, slot.texW, slot.texH, 1, slot.pitch};
            to.pResource = slot.texture.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
            slot.uploadedVersion = layer.frameVersion;
        }
        states.transition(list, slot.texture.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (!bound && (p.boundY != y || p.boundUv != uv)) {
            pipeline::makeUav(p.device, y, yFormat, pipeline::cpuHandleOf(p.pass, Impl::kUav));
            pipeline::makeUav(p.device, uv, uvFormat, pipeline::cpuHandleOf(p.pass, Impl::kUav + 1));
            p.boundY = y; p.boundUv = uv;
        }
        bound = true;
        // The picture under the rectangle, at the copies' top left.
        states.transition(list, y, D3D12_RESOURCE_STATE_COPY_SOURCE);
        states.transition(list, uv, D3D12_RESOURCE_STATE_COPY_SOURCE);
        states.transition(list, p.bgY.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        states.transition(list, p.bgUv.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION from{}, to{};
        from.Type = to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        from.pResource = y; to.pResource = p.bgY.Get();
        const D3D12_BOX yBox{x0, y0, 0, x0 + w, y0 + h, 1};
        list->CopyTextureRegion(&to, 0, 0, 0, &from, &yBox);
        from.pResource = uv; to.pResource = p.bgUv.Get();
        const D3D12_BOX uvBox{x0 / 2, y0 / 2, 0, (x0 + w) / 2, (y0 + h) / 2, 1};
        list->CopyTextureRegion(&to, 0, 0, 0, &from, &uvBox);
        states.transition(list, p.bgY.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        states.transition(list, p.bgUv.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        states.transition(list, y, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        states.transition(list, uv, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const uint32_t taps = std::clamp<uint32_t>(uint32_t(std::ceil(std::max(double(slot.texW) / w, double(slot.texH) / h))), 1u, 4u);
        const uint32_t constants[8] = {x0, y0, w, h, slot.texW, slot.texH, (p.hdr ? 1u : 0u) | (taps << 8),
                                       std::bit_cast<uint32_t>(std::clamp(layer.opacity, 0.0f, 1.0f))};
        float bits[8];
        std::memcpy(bits, constants, sizeof(bits));
        p.pass.bind(list, bits, pipeline::gpuHandleOf(p.pass, UINT((i * Impl::kSets + slot.set) * 3)).ptr,
                    pipeline::gpuHandleOf(p.pass, Impl::kUav).ptr);
        list->Dispatch((w + 15) / 16, (h + 15) / 16, 1);
        states.uavBarrier(list, y);
        states.uavBarrier(list, uv);
    }
}

} // namespace veyra::engine
