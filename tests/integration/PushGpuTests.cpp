// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS GPU pieces on a real D3D12 device (hardware, or WARP on build
// machines without a graphics card). VEYRA_PUSH_GPU_DEBUG=1 enables the D3D12
// debug layer (when installed) and fails on its errors.
//  - RecordingCompare (before/after comparison clips): a real frame is held,
//    a generated frame gets the held real frame in its left half and keeps its
//    own right half, the divider and both labels land where expected in luma
//    and neutral chroma, a later real frame replaces the hold; an HDR clip
//    splits without overlays.
//  - PushHudSampler (automatic HUD detection): copies only real frames, one at
//    a time, at the interval; finds a static HUD box on a moving picture end to
//    end; refuses a format it cannot read.
//  - PushHudLock (Live HUD Lock): after a second of a still HUD over a moving
//    picture, a generated frame takes the HUD pixels from the held real frame
//    and keeps its own everywhere else; a HUD that disappears is released on
//    that frame; a reset starts over.
#include "veyra/engine/PushHudSampler.h"
#include "veyra/pipeline/PushHudLock.h"
#include "veyra/engine/RecordingCompare.h"
#include "veyra/pipeline/GpuPassUtils.h"

#include <d3d12sdklayers.h>
#include <dxgi1_6.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace veyra;
using Microsoft::WRL::ComPtr;

namespace {
int failures = 0;
void check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}

struct Gpu {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE event = nullptr;
    uint64_t value = 0;
    bool debug = false;
    std::string adapter;

    bool create() {
        if (GetEnvironmentVariableW(L"VEYRA_PUSH_GPU_DEBUG", nullptr, 0) > 0) {
            ComPtr<ID3D12Debug> d;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&d)))) { d->EnableDebugLayer(); debug = true; }
            else std::printf("NOTE D3D12 debug layer not installed; running without it\n");
        }
        ComPtr<IDXGIFactory6> factory;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return false;
        for (UINT i = 0; !device; ++i) {
            ComPtr<IDXGIAdapter1> a;
            if (factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d);
            if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            if (SUCCEEDED(D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) adapter = narrow(d.Description);
        }
        if (!device) {
            ComPtr<IDXGIAdapter> warp;
            if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
                FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) return false;
            adapter = "WARP";
        }
        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        return SUCCEEDED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))) &&
               SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) &&
               SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))) &&
               SUCCEEDED(list->Close()) &&
               SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) &&
               (event = CreateEventW(nullptr, FALSE, FALSE, nullptr)) != nullptr;
    }
    static std::string narrow(const wchar_t* w) {
        std::string s; for (; *w; ++w) s.push_back(*w < 128 ? char(*w) : '?'); return s;
    }
    bool begin() { return SUCCEEDED(allocator->Reset()) && SUCCEEDED(list->Reset(allocator.Get(), nullptr)); }
    bool submit() {
        if (FAILED(list->Close())) return false;
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        return SUCCEEDED(queue->Signal(fence.Get(), ++value));
    }
    bool wait() {
        if (fence->GetCompletedValue() < value) { fence->SetEventOnCompletion(value, event); WaitForSingleObject(event, 60000); }
        return fence->GetCompletedValue() >= value && SUCCEEDED(device->GetDeviceRemovedReason());
    }
    bool submitAndWait() { return submit() && wait(); }
    bool debugErrors() {
        if (!debug) return false;
        ComPtr<ID3D12InfoQueue> info;
        if (FAILED(device.As(&info))) return false;
        bool errors = false;
        for (UINT64 i = 0, n = info->GetNumStoredMessages(); i < n; ++i) {
            SIZE_T size = 0; info->GetMessage(i, nullptr, &size);
            std::vector<char> buffer(size); auto* m = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
            if (SUCCEEDED(info->GetMessage(i, m, &size)) && m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
                std::printf("D3D12 %s\n", m->pDescription); errors = true;
            }
        }
        info->ClearStoredMessages();
        return errors;
    }
};

ComPtr<ID3D12Resource> makeBuffer(ID3D12Device* device, uint64_t bytes, D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = type;
    D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = bytes; bd.Height = 1;
    bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> r;
    const auto state = type == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, state, nullptr, IID_PPV_ARGS(&r)))) return nullptr;
    return r;
}

// A texture plane: bytes per pixel `bpp`, filled or read through a buffer.
struct Plane {
    ComPtr<ID3D12Resource> texture;
    DXGI_FORMAT format;
    uint32_t w, h, bpp;
    UINT pitch() const { return (w * bpp + 255) & ~255u; }
};

// Records an upload of `pixels` (tight rows) into the plane; the texture ends in `after`.
ComPtr<ID3D12Resource> recordFill(Gpu& gpu, const Plane& p, const std::vector<uint8_t>& pixels, D3D12_RESOURCE_STATES before,
                                  D3D12_RESOURCE_STATES after) {
    auto upload = makeBuffer(gpu.device.Get(), uint64_t(p.pitch()) * p.h, D3D12_HEAP_TYPE_UPLOAD);
    if (!upload) return nullptr;
    uint8_t* mapped = nullptr;
    upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    for (uint32_t y = 0; y < p.h; ++y) std::memcpy(mapped + size_t(y) * p.pitch(), pixels.data() + size_t(y) * p.w * p.bpp, size_t(p.w) * p.bpp);
    upload->Unmap(0, nullptr);
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = p.texture.Get();
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    if (before != D3D12_RESOURCE_STATE_COPY_DEST) {
        b.Transition.StateBefore = before; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST; gpu.list->ResourceBarrier(1, &b);
    }
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = upload.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint.Footprint = {p.format, p.w, p.h, 1, p.pitch()};
    to.pResource = p.texture.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    gpu.list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    if (after != D3D12_RESOURCE_STATE_COPY_DEST) {
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = after; gpu.list->ResourceBarrier(1, &b);
    }
    return upload;
}

// Records a readback of the plane (currently in `state`, left there) into a new buffer.
ComPtr<ID3D12Resource> recordRead(Gpu& gpu, const Plane& p, D3D12_RESOURCE_STATES state) {
    auto readback = makeBuffer(gpu.device.Get(), uint64_t(p.pitch()) * p.h, D3D12_HEAP_TYPE_READBACK);
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = p.texture.Get();
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    if (state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        b.Transition.StateBefore = state; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE; gpu.list->ResourceBarrier(1, &b);
    }
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = p.texture.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource = readback.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint.Footprint = {p.format, p.w, p.h, 1, p.pitch()};
    gpu.list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    if (state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter); gpu.list->ResourceBarrier(1, &b);
    }
    return readback;
}

std::vector<uint8_t> mapRead(const Plane& p, ID3D12Resource* readback) {
    std::vector<uint8_t> out(size_t(p.w) * p.h * p.bpp);
    uint8_t* mapped = nullptr;
    if (FAILED(readback->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) return {};
    for (uint32_t y = 0; y < p.h; ++y) std::memcpy(out.data() + size_t(y) * p.w * p.bpp, mapped + size_t(y) * p.pitch(), size_t(p.w) * p.bpp);
    const D3D12_RANGE none{0, 0};
    readback->Unmap(0, &none);
    return out;
}

void testCompare(Gpu& gpu) {
    constexpr uint32_t W = 256, H = 144;
    auto* device = gpu.device.Get();
    Plane y{pipeline::makeTexture(device, W, H, DXGI_FORMAT_R8_UNORM, true), DXGI_FORMAT_R8_UNORM, W, H, 1};
    Plane uv{pipeline::makeTexture(device, W / 2, H / 2, DXGI_FORMAT_R8G8_UNORM, true), DXGI_FORMAT_R8G8_UNORM, W / 2, H / 2, 2};
    check(y.texture && uv.texture, "compare: planes created");
    engine::RecordingLabel left{8, 4, std::vector<uint8_t>(32, 255)}, right{9, 5, std::vector<uint8_t>(45, 0)};
    engine::RecordingCompare compare;
    std::wstring reason;
    check(compare.create(device, W, H, false, left, right, reason), "compare: created");
    check(compare.labelled() && compare.patches().size() == 3, "compare: divider and two labels");
    check(compare.splitX() == 128, "compare: split in the middle");

    pipeline::StateTracker states;
    auto frame = [&](uint8_t luma, uint8_t chroma, bool generated, std::vector<uint8_t>* yOut, std::vector<uint8_t>* uvOut) {
        gpu.begin();
        // The recorder's planes are UAVs written by the conversion; fill them like it.
        auto a = recordFill(gpu, y, std::vector<uint8_t>(W * H, luma), states.get(y.texture.Get()), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        states.set(y.texture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        auto b = recordFill(gpu, uv, std::vector<uint8_t>(W / 2 * H / 2 * 2, chroma), states.get(uv.texture.Get()), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        states.set(uv.texture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        compare.compose(gpu.list.Get(), states, y.texture.Get(), uv.texture.Get(), generated);
        ComPtr<ID3D12Resource> ry, ruv;
        if (yOut) { ry = recordRead(gpu, y, states.get(y.texture.Get())); ruv = recordRead(gpu, uv, states.get(uv.texture.Get())); }
        const bool ok = gpu.submitAndWait();
        if (yOut && ok) { *yOut = mapRead(y, ry.Get()); *uvOut = mapRead(uv, ruv.Get()); }
        return ok;
    };
    std::vector<uint8_t> py, puv;
    check(frame(50, 100, false, &py, &puv), "compare: real frame runs");
    check(py.size() == W * H && py[60 * W + 2] == 50 && py[60 * W + 200] == 50, "compare: a real frame stays whole");
    check(frame(200, 150, true, &py, &puv), "compare: generated frame runs");
    const auto Y = [&](uint32_t x, uint32_t yy) { return int(py[size_t(yy) * W + x]); };
    const auto U = [&](uint32_t x, uint32_t yy) { return int(puv[(size_t(yy) * (W / 2) + x) * 2]); };
    check(Y(2, 60) == 50 && Y(100, 120) == 50, "compare: left half holds the real frame");
    check(Y(200, 60) == 200 && Y(250, 140) == 200, "compare: right half is the generated frame");
    check(Y(126, 60) == 235 && Y(129, 60) == 235 && Y(125, 60) == 50 && Y(130, 60) == 200, "compare: divider is four white pixels at the split");
    check(Y(10, 10) == 235 && Y(8, 8) == 235 && Y(15, 11) == 235 && Y(16, 10) == 50, "compare: left label (white) at the top left");
    check(Y(240, 8) == 16 && Y(247, 11) == 16 && Y(248, 10) == 200 && Y(239, 10) == 200, "compare: right label (black, odd size trimmed) at the top right");
    check(U(1, 30) == 100 && U(100, 30) == 150 && U(5, 5) == 128 && U(64, 30) == 128, "compare: chroma follows and overlays are neutral");
    check(compare.generatedFrames() == 1 && compare.realFrames() == 1, "compare: counts frames");
    check(frame(90, 120, false, &py, &puv), "compare: next real frame runs");
    check(Y(2, 60) == 90 && Y(200, 60) == 90, "compare: a new real frame replaces the hold");
    check(frame(10, 10, true, &py, &puv) && Y(2, 60) == 90 && Y(200, 60) == 10, "compare: the next generated frame uses the new hold");
    check(!gpu.debugErrors(), "compare: no D3D12 debug errors");

    engine::RecordingLabel huge{200, 100, std::vector<uint8_t>(20000, 255)};
    engine::RecordingCompare tooBig;
    check(tooBig.create(device, W, H, false, huge, huge, reason) && tooBig.patches().size() == 3, "compare: labels too large are scaled down, not left out");
    if (tooBig.patches().size() == 3) {
        const auto& l = tooBig.patches()[1];
        check(l.width <= 128 - 2 * 8 - 4 && l.height <= H / 3 - 16 && l.width >= 8, "compare: the scaled label fits its half");
        check(tooBig.patches()[2].x + tooBig.patches()[2].width <= W - 8, "compare: the scaled right label stays inside the picture");
    }
    engine::RecordingLabel nothing{};
    engine::RecordingCompare unlabelled;
    check(unlabelled.create(device, W, H, false, nothing, nothing, reason) && unlabelled.patches().size() == 1, "compare: no label text keeps only the divider");
}

// One comparison frame on planes of any format: fill y and uv with the given bytes,
// compose, read both back (tight rows).
bool composeFrame(Gpu& gpu, engine::RecordingCompare& compare, pipeline::StateTracker& states, const Plane& y, const Plane& uv,
                  const std::vector<uint8_t>& yData, const std::vector<uint8_t>& uvData, bool generated,
                  std::vector<uint8_t>& yOut, std::vector<uint8_t>& uvOut) {
    if (!gpu.begin()) return false;
    auto a = recordFill(gpu, y, yData, states.get(y.texture.Get()), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    states.set(y.texture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto b = recordFill(gpu, uv, uvData, states.get(uv.texture.Get()), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    states.set(uv.texture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    compare.compose(gpu.list.Get(), states, y.texture.Get(), uv.texture.Get(), generated);
    auto ry = recordRead(gpu, y, states.get(y.texture.Get()));
    auto ruv = recordRead(gpu, uv, states.get(uv.texture.Get()));
    if (!gpu.submitAndWait()) return false;
    yOut = mapRead(y, ry.Get()); uvOut = mapRead(uv, ruv.Get());
    return yOut.size() == size_t(y.w) * y.h * y.bpp;
}
std::vector<uint8_t> words(const std::vector<uint16_t>& v) {
    std::vector<uint8_t> out(v.size() * 2);
    std::memcpy(out.data(), v.data(), out.size());
    return out;
}
uint16_t word(const std::vector<uint8_t>& bytes, size_t index) { uint16_t v; std::memcpy(&v, bytes.data() + index * 2, 2); return v; }

void testCompareHdr(Gpu& gpu) {
    // P010: 10-bit codes in the top bits. White text and the divider at code 573 (203 nits).
    constexpr uint32_t W = 256, H = 144;
    auto* device = gpu.device.Get();
    Plane y{pipeline::makeTexture(device, W, H, DXGI_FORMAT_R16_UNORM, true), DXGI_FORMAT_R16_UNORM, W, H, 2};
    Plane uv{pipeline::makeTexture(device, W / 2, H / 2, DXGI_FORMAT_R16G16_UNORM, true), DXGI_FORMAT_R16G16_UNORM, W / 2, H / 2, 4};
    check(y.texture && uv.texture, "compare HDR: planes created");
    engine::RecordingLabel left{8, 4, std::vector<uint8_t>(32, 255)}, right{8, 4, std::vector<uint8_t>(32, 0)};
    engine::RecordingCompare compare;
    std::wstring reason;
    check(compare.create(device, W, H, true, left, right, reason), "compare HDR: created");
    check(compare.labelled() && compare.patches().size() == 3, "compare HDR: divider and both labels (they used to be left out)");
    pipeline::StateTracker states;
    std::vector<uint8_t> py, puv;
    const uint16_t real = 300 << 6, gen = 700 << 6, chroma = 600 << 6;
    check(composeFrame(gpu, compare, states, y, uv, words(std::vector<uint16_t>(W * H, real)), words(std::vector<uint16_t>(W / 2 * H / 2 * 2, chroma)), false, py, puv), "compare HDR: real frame runs");
    check(composeFrame(gpu, compare, states, y, uv, words(std::vector<uint16_t>(W * H, gen)), words(std::vector<uint16_t>(W / 2 * H / 2 * 2, chroma)), true, py, puv), "compare HDR: generated frame runs");
    const auto Y = [&](uint32_t x, uint32_t yy) { return int(word(py, size_t(yy) * W + x)); };
    check(Y(2, 60) == real && Y(200, 60) == gen, "compare HDR: split holds the real frame on the left");
    check(Y(127, 60) == (573 << 6), "compare HDR: divider at 203 nits");
    check(Y(10, 10) == (573 << 6), "compare HDR: white label text at 203 nits");
    check(Y(240, 9) == (64 << 6), "compare HDR: black label box at code 64");
    check(int(word(puv, size_t(5) * (W / 2) * 2 + 5 * 2)) == (512 << 6), "compare HDR: overlay chroma is neutral");
    check(!gpu.debugErrors(), "compare HDR: no D3D12 debug errors");
}

void testCompareSideBySide(Gpu& gpu, bool hdr) {
    constexpr uint32_t W = 256, H = 144;
    const char* tag = hdr ? "side by side HDR" : "side by side";
    auto* device = gpu.device.Get();
    const auto yFormat = hdr ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
    const auto uvFormat = hdr ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
    const uint32_t bpp = hdr ? 2 : 1;
    Plane y{pipeline::makeTexture(device, W, H, yFormat, true), yFormat, W, H, bpp};
    Plane uv{pipeline::makeTexture(device, W / 2, H / 2, uvFormat, true), uvFormat, W / 2, H / 2, bpp * 2};
    engine::RecordingLabel left{8, 4, std::vector<uint8_t>(32, 255)}, right{8, 4, std::vector<uint8_t>(32, 255)};
    engine::RecordingCompare compare;
    std::wstring reason;
    const bool created = compare.create(device, W, H, hdr, left, right, reason, engine::CompareLayout::SideBySide);
    if (!created) std::printf("%s: create failed: %ls\n", tag, reason.c_str());
    check(created, std::string(tag) + ": created (shader loaded)");
    if (!created) return;
    check(compare.pictureWidth() == 128 && compare.pictureHeight() == 72 && compare.pictureTop() == 36 && compare.rightX() == 128, std::string(tag) + ": two half-size pictures, centred");
    check(compare.patches().size() == 3, std::string(tag) + ": divider and two labels");
    if (compare.patches().size() == 3) {
        const auto& d = compare.patches()[0]; const auto& l = compare.patches()[1]; const auto& r = compare.patches()[2];
        check(d.y == 36 && d.height == 72, std::string(tag) + ": divider only between the pictures");
        check(l.y + l.height <= 36 && r.y + r.height <= 36, std::string(tag) + ": labels in the black bar above the pictures");
        check(l.x == 60 && r.x == 188, std::string(tag) + ": labels centred over their picture");
    }
    // Values: SDR in 8-bit codes, HDR in 10-bit codes (stored << 6).
    const auto code = [&](uint32_t v8, uint32_t v10) { return hdr ? v10 << 6 : v8; };
    const auto fill = [&](auto f, uint32_t w, uint32_t h, uint32_t channels) {
        std::vector<uint8_t> out(size_t(w) * h * channels * bpp);
        for (uint32_t yy = 0; yy < h; ++yy)
            for (uint32_t x = 0; x < w; ++x)
                for (uint32_t c = 0; c < channels; ++c) {
                    const uint32_t v = f(x, yy);
                    const size_t at = (size_t(yy) * w + x) * channels + c;
                    if (hdr) { const uint16_t w16 = uint16_t(v); std::memcpy(out.data() + at * 2, &w16, 2); }
                    else out[at] = uint8_t(v);
                }
        return out;
    };
    const auto read = [&](const std::vector<uint8_t>& bytes, size_t index) { return hdr ? int(word(bytes, index)) : int(bytes[index]); };
    pipeline::StateTracker states;
    std::vector<uint8_t> py, puv;
    // Real frame: luma pairs of equal columns (so the 2x2 average is exact), chroma 100.
    const auto realY = fill([&](uint32_t x, uint32_t) { return code(x & ~1u, (x & ~1u) + 64); }, W, H, 1);
    const auto realUv = fill([&](uint32_t, uint32_t) { return code(100, 400); }, W / 2, H / 2, 2);
    check(composeFrame(gpu, compare, states, y, uv, realY, realUv, false, py, puv), std::string(tag) + ": real frame runs");
    const auto Y = [&](uint32_t x, uint32_t yy) { return read(py, size_t(yy) * W + x); };
    const auto U = [&](uint32_t x, uint32_t yy) { return read(puv, (size_t(yy) * (W / 2) + x) * 2); };
    check(Y(10, 41) == int(code(20, 84)), std::string(tag) + ": left picture is the frame at half size (2x2 average)");
    check(Y(138, 41) == int(code(20, 84)), std::string(tag) + ": a real frame shows on both sides");
    check(Y(5, 5) == int(code(16, 64)) && Y(5, 140) == int(code(16, 64)), std::string(tag) + ": black bars above and below");
    check(U(2, 2) == int(code(128, 512)) && U(10, 25) == int(code(100, 400)), std::string(tag) + ": chroma neutral in the bars, kept in the picture");
    check(Y(62, 17) == int(code(235, 573)), std::string(tag) + ": label text in the bar");
    check(Y(127, 60) == int(code(235, 573)) && Y(127, 20) == int(code(16, 64)), std::string(tag) + ": divider between the pictures only");
    // Generated frame: the right side changes, the left keeps the held real frame.
    const auto genY = fill([&](uint32_t, uint32_t) { return code(200, 800); }, W, H, 1);
    const auto genUv = fill([&](uint32_t, uint32_t) { return code(150, 600); }, W / 2, H / 2, 2);
    check(composeFrame(gpu, compare, states, y, uv, genY, genUv, true, py, puv), std::string(tag) + ": generated frame runs");
    check(Y(10, 41) == int(code(20, 84)), std::string(tag) + ": left keeps the real frame");
    check(Y(138, 41) == int(code(200, 800)) && U(70, 25) == int(code(150, 600)), std::string(tag) + ": right shows the generated frame");
    check(U(10, 25) == int(code(100, 400)), std::string(tag) + ": left chroma stays the real frame's");
    check(compare.generatedFrames() == 1 && compare.realFrames() == 1, std::string(tag) + ": counts frames");
    check(!gpu.debugErrors(), std::string(tag) + ": no D3D12 debug errors");
}

void testLabelScale() {
    engine::RecordingLabel l{8, 4, std::vector<uint8_t>(32, 0)};
    for (int yy = 0; yy < 4; ++yy) for (int x = 4; x < 8; ++x) l.luma[size_t(yy) * 8 + x] = 255;
    const auto h = engine::scaleRecordingLabel(l, 4, 2);
    check(h.width == 4 && h.height == 2 && h.luma[0] == 0 && h.luma[3] == 255, "label scale: halves by area average");
    const auto t = engine::scaleRecordingLabel(l, 3, 3);
    check(t.luma.size() == 9 && t.luma[1] == 128, "label scale: partial pixels are weighted");
    check(engine::scaleRecordingLabel(l, 0, 3).luma.empty(), "label scale: empty size gives no label");
}

void testHudSampler(Gpu& gpu) {
    constexpr uint32_t W = 640, H = 360;
    auto* device = gpu.device.Get();
    Plane frameTex{pipeline::makeTexture(device, W, H, DXGI_FORMAT_R8G8B8A8_UNORM, false), DXGI_FORMAT_R8G8B8A8_UNORM, W, H, 4};
    check(frameTex.texture != nullptr, "hud: frame texture created");
    engine::PushHudSampler sampler;
    // Not running: nothing recorded.
    gpu.begin();
    const engine::PresentCapture::Frame idle{frameTex.texture.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, W, H, 0, 0, false};
    check(!sampler.record(gpu.list.Get(), idle), "hud: nothing is copied while no detection runs");
    gpu.list->Close();

    sampler.start(6, 60);
    // HUD box: x 40..200, y 20..60 (static text-like strokes); the rest moves.
    auto picture = [&](int shift) {
        std::vector<uint8_t> px(size_t(W) * H * 4);
        for (uint32_t yy = 0; yy < H; ++yy)
            for (uint32_t x = 0; x < W; ++x) {
                uint8_t v;
                if (x >= 40 && x < 200 && yy >= 20 && yy < 60) v = ((x / 4 + yy / 5) % 3 == 0 || x % 9 == 0) ? 235 : 20;
                else {
                    const int sx = int(x) + shift, sy = int(yy) + shift / 2;
                    uint32_t h = uint32_t(sx / 2) * 374761393u + uint32_t(sy / 2) * 668265263u;
                    h = (h ^ (h >> 13)) * 1274126177u;
                    v = uint8_t(std::clamp(int(128 + 60 * std::sin(sx * 0.03) + 40 * std::cos(sy * 0.04)) + int(h >> 28) * 4 - 30, 0, 255));
                }
                uint8_t* p = px.data() + (size_t(yy) * W + x) * 4;
                p[0] = p[1] = p[2] = v; p[3] = 255;
            }
        return px;
    };
    int copies = 0, refusedGenerated = 0, refusedBusy = 0;
    engine::PushHudSampler::Status status;
    for (int step = 0; step < 200 && !status.done && !status.failed; ++step) {
        gpu.begin();
        auto upload = recordFill(gpu, frameTex, picture(step * 7), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);
        const engine::PresentCapture::Frame generated{frameTex.texture.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, W, H, 0, 0, true};
        if (sampler.record(gpu.list.Get(), generated)) { sampler.submitted(gpu.fence.Get(), gpu.value + 1); ++refusedGenerated; } // counts a wrong copy
        const engine::PresentCapture::Frame real{frameTex.texture.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, W, H, 0, 0, false};
        const bool recorded = sampler.record(gpu.list.Get(), real);
        if (recorded) {
            ++copies;
            if (sampler.record(gpu.list.Get(), real)) ++refusedBusy; // a second copy in the same list must be refused
        }
        if (!gpu.submit()) break;
        if (recorded) sampler.submitted(gpu.fence.Get(), gpu.value);
        gpu.wait();
        status = sampler.status();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(refusedGenerated == 0, "hud: generated frames are never copied");
    check(refusedBusy == 0, "hud: one copy at a time");
    check(status.done && !status.failed && copies == 6 && status.taken == 6, "hud: six frames taken, then done");
    check(status.preview.size() == size_t(engine::hud::kPreviewWidth) * engine::hud::kPreviewHeight * 3, "hud: preview picture of the last frame");
    std::printf("INFO hud motion=%.2f regions=%zu\n", status.detection.motion, status.detection.regions.size());
    bool found = false;
    for (const auto& r : status.detection.regions)
        found = found || (r.left <= 40.0f / W + 0.01f && r.top <= 20.0f / H + 0.01f && r.right >= 200.0f / W - 0.01f && r.bottom >= 60.0f / H - 0.01f &&
                          r.right <= 230.0f / W && r.bottom <= 80.0f / H);
    check(found, "hud: the static box is found end to end");
    check(!gpu.debugErrors(), "hud: no D3D12 debug errors");

    // A discarded list leaves the sampler free for the next frame.
    sampler.start(6, 50);
    gpu.begin();
    const engine::PresentCapture::Frame real{frameTex.texture.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, W, H, 0, 0, false};
    check(sampler.record(gpu.list.Get(), real), "hud: copies after a restart");
    sampler.discarded();
    check(sampler.record(gpu.list.Get(), real), "hud: a discarded copy frees the slot");
    gpu.list->Close();
    sampler.discarded();
    sampler.cancel();
    check(!sampler.status().running, "hud: cancel stops it");

    // A format it cannot read fails with a message.
    Plane floatTex{pipeline::makeTexture(device, 64, 64, DXGI_FORMAT_R32_FLOAT, false), DXGI_FORMAT_R32_FLOAT, 64, 64, 4};
    sampler.start(6, 50);
    gpu.begin();
    const engine::PresentCapture::Frame odd{floatTex.texture.Get(), DXGI_FORMAT_R32_FLOAT, 64, 64, 0, 0, false};
    check(!sampler.record(gpu.list.Get(), odd), "hud: an unsupported format is not copied");
    gpu.list->Close();
    const auto failed = sampler.status();
    check(failed.failed && !failed.error.empty() && !failed.running, "hud: an unsupported format fails with a reason");
}
void testHudLock(Gpu& gpu) {
    constexpr uint32_t W = 256, H = 144;
    constexpr DXGI_FORMAT F = DXGI_FORMAT_R8G8B8A8_UNORM;
    auto* device = gpu.device.Get();
    Plane real[2]{{pipeline::makeTexture(device, W, H, F, true), F, W, H, 4}, {pipeline::makeTexture(device, W, H, F, true), F, W, H, 4}};
    Plane generated{pipeline::makeTexture(device, W, H, F, true), F, W, H, 4};
    check(real[0].texture && real[1].texture && generated.texture, "hud lock: textures created");
    pipeline::PushHudLock lock;
    check(lock.initialize(device, W, H, F, 0), "hud lock: created");
    pipeline::DescriptorStager stager;
    check(stager.initialize(device, 64), "hud lock: staging heap");
    ID3D12Resource* reals[2] = {real[0].texture.Get(), real[1].texture.Get()};
    ID3D12Resource* gens[1] = {generated.texture.Get()};
    lock.createViews(device, stager, reals, gens, 1);
    // HUD: x 20..100, y 10..40, high-contrast stripes that never move. Everything else moves.
    auto hudAt = [](uint32_t x, uint32_t y) { return x >= 20 && x < 100 && y >= 10 && y < 40; };
    auto picture = [&](int shift, bool hud) {
        std::vector<uint8_t> px(size_t(W) * H * 4);
        for (uint32_t y = 0; y < H; ++y)
            for (uint32_t x = 0; x < W; ++x) {
                uint8_t v;
                if (hud && hudAt(x, y)) v = ((x / 3) % 2) ? 230 : 25;
                else {
                    uint32_t h = uint32_t(int(x) + shift) * 374761393u + uint32_t(int(y) + shift / 3) * 668265263u;
                    h = (h ^ (h >> 13)) * 1274126177u;
                    v = uint8_t(40 + (h >> 25));
                }
                uint8_t* p = px.data() + (size_t(y) * W + x) * 4;
                p[0] = p[1] = p[2] = v; p[3] = 255;
            }
        return px;
    };
    pipeline::StateTracker states;
    std::vector<uint8_t> last;
    auto realFrame = [&](int i, bool hud, bool reset) {
        const unsigned parity = unsigned(i) & 1u;
        gpu.begin();
        last = picture(i * 5, hud);
        auto up = recordFill(gpu, real[parity], last, states.get(real[parity].texture.Get()), D3D12_RESOURCE_STATE_COMMON);
        states.set(real[parity].texture.Get(), D3D12_RESOURCE_STATE_COMMON);
        const bool ok = lock.update(gpu.list.Get(), states, parity, reset);
        return ok && gpu.submitAndWait();
    };
    auto generatedFrame = [&](unsigned held, std::vector<uint8_t>& out) {
        gpu.begin();
        auto up = recordFill(gpu, generated, std::vector<uint8_t>(size_t(W) * H * 4, 128), states.get(generated.texture.Get()), D3D12_RESOURCE_STATE_COMMON);
        states.set(generated.texture.Get(), D3D12_RESOURCE_STATE_COMMON);
        const bool ok = lock.compose(gpu.list.Get(), states, 0, held);
        auto rb = recordRead(gpu, generated, states.get(generated.texture.Get()));
        if (!ok || !gpu.submitAndWait()) return false;
        out = mapRead(generated, rb.Get());
        return true;
    };
    bool ran = true;
    int i = 0;
    for (; i < 60; ++i) ran = ran && realFrame(i, true, i == 0);
    check(ran, "hud lock: 60 real frames update the state");
    std::vector<uint8_t> out;
    check(generatedFrame(unsigned(i - 1) & 1u, out), "hud lock: a generated frame is composed");
    int hudHeld = 0, hudTotal = 0, bgKept = 0, bgTotal = 0;
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            const size_t k = (size_t(y) * W + x) * 4;
            if (hudAt(x, y)) { ++hudTotal; hudHeld += out[k] == last[k]; }
            else if (x >= 110 || y >= 50) { ++bgTotal; bgKept += out[k] == 128; }
        }
    std::printf("INFO hud lock: HUD pixels from the real frame %d/%d, moving pixels kept %d/%d\n", hudHeld, hudTotal, bgKept, bgTotal);
    check(hudHeld >= hudTotal * 95 / 100, "hud lock: the HUD comes from the held real frame");
    check(bgKept == bgTotal, "hud lock: the moving picture keeps the generated pixels");
    // The HUD goes away (a menu closes): released on that very frame.
    check(realFrame(i, false, false), "hud lock: a frame without the HUD runs");
    check(generatedFrame(unsigned(i) & 1u, out), "hud lock: composed after the HUD went away");
    int released = 0;
    for (uint32_t y = 12; y < 38; ++y) for (uint32_t x = 22; x < 98; ++x) released += out[(size_t(y) * W + x) * 4] == 128;
    check(released >= 26 * 76 * 95 / 100, "hud lock: a HUD that moves or disappears is released at once (" + std::to_string(released) + "/1976)");
    // A reset starts over: nothing is locked right after it.
    ++i;
    check(realFrame(i, true, true), "hud lock: reset runs");
    check(generatedFrame(unsigned(i) & 1u, out), "hud lock: composed after a reset");
    int afterReset = 0;
    for (size_t k = 0; k < out.size(); k += 4) afterReset += out[k] != 128;
    check(afterReset == 0, "hud lock: nothing is locked right after a reset");
    check(!gpu.debugErrors(), "hud lock: no D3D12 debug errors");
}
} // namespace

int main() {
    Gpu gpu;
    if (!gpu.create()) { std::printf("FAIL no D3D12 device\n"); return 1; }
    std::printf("INFO adapter %s debug=%d\n", gpu.adapter.c_str(), gpu.debug ? 1 : 0);
    testCompare(gpu);
    testCompareHdr(gpu);
    testCompareSideBySide(gpu, false);
    testCompareSideBySide(gpu, true);
    testLabelScale();
    testHudSampler(gpu);
    testHudLock(gpu);
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
