// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFlowBench: measures PushFlow (PushFPS's own frame generation) on this PC's graphics card and writes
// the results as JSON, so players can share real numbers (docs/PUSHFLOW.md, "Measure your card").
// For each size and tier: warm-up, then N pairs at 2X/3X/4X with D3D12 timestamp queries around the pack
// and rebuild passes and the network's span on the queue, plus the CPU time of the inference call.
// Usage: PushFlowBench.exe [--sizes 1920x1080,2560x1440,3840x2160] [--tiers 0,1,2] [--pairs 40] [--compare-shapes] [--compare-sync]
//                          [--out pushflow-bench.json] [--warp] [--runtime <folder holding pushflow>]
// Runs next to PushFPS.exe (it uses PushFPS's shaders and runtime\experimental\pushflow).
#include "veyra/pushflow/PushFlowBackend.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/RuntimePaths.h"

#include <windows.h>
#include <psapi.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <fstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace veyra;

namespace {
struct Gpu {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator[2];
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12QueryHeap> queries;
    ComPtr<ID3D12Resource> readback;
    HANDLE event = nullptr;
    uint64_t value = 0, frequency = 1;
    int current = 0;
    std::string adapter;
    uint64_t vram = 0;

    bool create(bool warp, uint64_t luid = 0) {
        ComPtr<IDXGIFactory6> factory;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return false;
        if (!warp && luid) {
            // The adapter PushFPS itself renders on (--adapter-luid), when it is still there.
            LUID l{}; l.LowPart = DWORD(luid & 0xffffffffu); l.HighPart = LONG(luid >> 32);
            ComPtr<IDXGIAdapter1> a;
            if (SUCCEEDED(factory->EnumAdapterByLuid(l, IID_PPV_ARGS(&a)))) {
                DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d);
                if (SUCCEEDED(D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
                    for (const wchar_t* c = d.Description; *c; ++c) adapter.push_back(*c < 128 ? char(*c) : '?');
                    vram = d.DedicatedVideoMemory;
                }
            }
        }
        for (UINT i = 0; !warp && !device; ++i) {
            ComPtr<IDXGIAdapter1> a;
            if (factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d);
            if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            if (SUCCEEDED(D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
                for (const wchar_t* c = d.Description; *c; ++c) adapter.push_back(*c < 128 ? char(*c) : '?');
                vram = d.DedicatedVideoMemory;
            }
        }
        if (!device) {
            ComPtr<IDXGIAdapter> w;
            if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&w))) || FAILED(D3D12CreateDevice(w.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) return false;
            adapter = "WARP (software)";
        }
        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) return false;
        for (auto& a : allocator) if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&a)))) return false;
        if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator[0].Get(), nullptr, IID_PPV_ARGS(&list))) || FAILED(list->Close())) return false;
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
        D3D12_QUERY_HEAP_DESC qh{}; qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qh.Count = 8;
        if (FAILED(device->CreateQueryHeap(&qh, IID_PPV_ARGS(&queries)))) return false;
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = 64; bd.Height = 1; bd.DepthOrArraySize = 1;
        bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return false;
        queue->GetTimestampFrequency(&frequency);
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return event != nullptr;
    }
    bool begin() {
        current ^= 1;
        return SUCCEEDED(allocator[current]->Reset()) && SUCCEEDED(list->Reset(allocator[current].Get(), nullptr));
    }
    bool submit() {
        if (FAILED(list->Close())) return false;
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        return SUCCEEDED(queue->Signal(fence.Get(), ++value));
    }
    bool wait() {
        if (fence->GetCompletedValue() < value) { fence->SetEventOnCompletion(value, event); WaitForSingleObject(event, 60000); }
        return fence->GetCompletedValue() >= value;
    }
    void stamp(UINT index) { list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index); }
};

ComPtr<ID3D12Resource> frameTexture(Gpu& gpu, uint32_t w, uint32_t h, double shift) {
    auto tex = pipeline::makeTexture(gpu.device.Get(), w, h, DXGI_FORMAT_R8G8B8A8_UNORM, false);
    const UINT pitch = (w * 4 + 255) & ~255u;
    auto upload = pipeline::makeUploadBuffer(gpu.device.Get(), uint64_t(pitch) * h);
    if (!tex || !upload) return nullptr;
    uint8_t* mapped = nullptr;
    upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* p = mapped + size_t(y) * pitch + size_t(x) * 4;
            const double u = x - shift;
            const uint32_t hsh = uint32_t(int64_t(u) * 73856093) ^ (y * 19349663u);
            const int v = 60 + int((hsh ^ (hsh >> 13)) * 1274126177u >> 25);
            p[0] = uint8_t(v); p[1] = uint8_t(v * 9 / 10 + 12); p[2] = uint8_t(v * 8 / 10 + 25); p[3] = 255;
        }
    upload->Unmap(0, nullptr);
    if (!gpu.begin()) return nullptr;
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = upload.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, w, h, 1, pitch};
    to.pResource = tex.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = tex.Get();
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    gpu.list->ResourceBarrier(1, &b);
    gpu.list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    gpu.list->ResourceBarrier(1, &b);
    if (!gpu.submit() || !gpu.wait()) return nullptr;
    return tex;
}

struct Samples {
    std::vector<double> v;
    void add(double x) { v.push_back(x); }
    double pct(double p) const {
        if (v.empty()) return 0;
        auto s = v; std::sort(s.begin(), s.end());
        return s[std::min(s.size() - 1, size_t(p * double(s.size() - 1) + 0.5))];
    }
};
std::string stats(const Samples& s) { return std::format("{{\"p50\": {:.3f}, \"p95\": {:.3f}, \"max\": {:.3f}, \"n\": {}}}", s.pct(0.5), s.pct(0.95), s.pct(1.0), s.v.size()); }
} // namespace

int main(int argc, char** argv) {
    std::vector<std::pair<uint32_t, uint32_t>> sizes{{1920, 1080}, {2560, 1440}, {3840, 2160}};
    std::vector<uint32_t> tiers{0, 1, 2};
    int pairs = 40;
    bool warp = false, compareShapes = false, compareSync = false, fp16Probe = false;
    uint64_t adapterLuid = 0;
    bool sizesGiven = false, pairsGiven = false;
    int leakCheck = 0, teardownCheck = 0;
    std::string out = "pushflow-bench.json";
    std::wstring runtimeOverride;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const std::string next = i + 1 < argc ? argv[i + 1] : "";
        if (a == "--warp") warp = true;
        else if (a == "--compare-shapes") compareShapes = true;
        else if (a == "--compare-sync") compareSync = true;
        else if (a == "--fp16-probe") fp16Probe = true;
        else if (a == "--leak-check" && !next.empty()) { leakCheck = std::clamp(std::atoi(next.c_str()), 40, 5000); ++i; }
        else if (a == "--teardown-check" && !next.empty()) { teardownCheck = std::clamp(std::atoi(next.c_str()), 1, 200); ++i; }
        else if (a == "--adapter-luid" && !next.empty()) { adapterLuid = std::strtoull(next.c_str(), nullptr, 10); ++i; }
        else if (a == "--runtime" && !next.empty()) { runtimeOverride.assign(next.begin(), next.end()); ++i; }
        else if (a == "--pairs" && !next.empty()) { pairs = std::clamp(std::atoi(next.c_str()), 1, 1000); pairsGiven = true; ++i; }
        else if (a == "--out" && !next.empty()) { out = next; ++i; }
        else if (a == "--tiers" && !next.empty()) {
            tiers.clear();
            for (char c : next) if (c >= '0' && c <= '2') tiers.push_back(uint32_t(c - '0'));
            ++i;
        } else if (a == "--sizes" && !next.empty()) {
            sizes.clear();
            sizesGiven = true;
            size_t pos = 0;
            while (pos < next.size()) {
                const size_t comma = next.find(',', pos);
                const std::string item = next.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                unsigned w = 0, h = 0;
                if (std::sscanf(item.c_str(), "%ux%u", &w, &h) == 2 && w >= 64 && h >= 64 && w <= 7680 && h <= 4320) sizes.push_back({w, h});
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
            ++i;
        } else if (a == "--help" || a == "-h") {
            std::printf("PushFlowBench [--sizes 1920x1080,2560x1440] [--tiers 0,1,2] [--pairs 40] [--out file.json] [--warp] [--compare-shapes] [--compare-sync]\n"
                        "PushFlowBench --fp16-probe [--adapter-luid <n>] --out result.json   (is half precision safe and faster here?)\n"
                        "PushFlowBench --teardown-check 10 --out result.json   (start, run and shut PushFlow down at once, 10 times per session kind)\n");
            return 0;
        }
    }
    Gpu gpu;
    if (!gpu.create(warp, adapterLuid)) { std::printf("No D3D12 device\n"); return 1; }
    // The folder holding pushflow\ (runtime\experimental next to PushFPS.exe), or --runtime <folder>.
    const std::wstring runtimeDir = !runtimeOverride.empty() ? runtimeOverride : runtime::localRuntimeDirectory().wstring();
    if (teardownCheck) {
        // --teardown-check N: N times per session kind PushFlow is started, runs a few queued inferences
        // (or none) and is shut down at once with its work still on the queue: how a settings rebuild,
        // an optimiser step or a stream that changes size right after connecting ends it. Counts the
        // ONNX Runtime faults caught on the way (each one ended PushFPS before 3.1.7; field dump of
        // 2026-11-17). Dynamic sessions keep one DirectML kernel per node, as every session did in 3.1.x.
        // VEYRA_PUSHFLOW_TEST_UNGUARDED=1 runs the teardown of 3.2.0 for comparison.
        const uint32_t w = sizes.empty() || !sizesGiven ? 256 : sizes[0].first, h = sizes.empty() || !sizesGiven ? 160 : sizes[0].second, tier = tiers.empty() ? 0 : tiers[0];
        auto a = frameTexture(gpu, w, h, 0), b = frameTexture(gpu, w, h, 8);
        auto output = pipeline::makeTexture(gpu.device.Get(), w, h, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        if (!a || !b || !output) { std::printf("teardown-check: textures failed\n"); return 1; }
        struct Kind { const char* name; bool dynamic; int runs; };
        const Kind kinds[] = {{"static, 3 runs", false, 3}, {"static, never run", false, 0}, {"dynamic, 3 runs", true, 3}, {"dynamic, 1 run", true, 1}};
        const bool unguarded = GetEnvironmentVariableW(L"VEYRA_PUSHFLOW_TEST_UNGUARDED", nullptr, 0) > 0;
        bool outputState = false, poolHeld = true;
        uint64_t started = 0, notStarted = 0, runFailures = 0;
        const uint64_t faultsBefore = pushflow::faultCount();
        std::string rows;
        for (const Kind& kind : kinds) {
            SetEnvironmentVariableW(L"VEYRA_PUSHFLOW_DYNAMIC", kind.dynamic ? L"1" : nullptr);
            const uint64_t before = pushflow::faultCount();
            uint64_t kindStarted = 0;
            for (int i = 0; i < teardownCheck; ++i) {
                if (gpu.device->GetDeviceRemovedReason() != S_OK) break;
                {
                    pushflow::PushFlowBackend flow;
                    if (!flow.initialize(gpu.device.Get(), gpu.queue.Get(), runtimeDir, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, pushflow::Encoding::Encoded, tier)) { ++notStarted; continue; }
                    ++started; ++kindStarted;
                    poolHeld = poolHeld && flow.status().poolHeld;
                    for (int r = 0; r < kind.runs; ++r) {
                        bool good = gpu.begin() && flow.pack(gpu.list.Get(), a.Get(), b.Get(), 0.5f, false) && gpu.submit() && flow.infer() && gpu.begin();
                        if (good) {
                            D3D12_RESOURCE_BARRIER bar{}; bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; bar.Transition.pResource = output.Get();
                            bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                            if (!outputState) { bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON; bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST; gpu.list->ResourceBarrier(1, &bar); outputState = true; }
                            good = flow.resolve(gpu.list.Get(), a.Get(), b.Get(), output.Get()) && gpu.submit();   // not waited for
                        }
                        if (!good) { ++runFailures; break; }
                    }
                }   // PushFlow shuts down here
                gpu.wait();
            }
            const uint64_t faults = pushflow::faultCount() - before;
            std::printf("teardown-check %-18s %llu started, %llu faults\n", kind.name, static_cast<unsigned long long>(kindStarted), static_cast<unsigned long long>(faults));
            rows += std::format("{}{{\"kind\": \"{}\", \"started\": {}, \"faults\": {}}}", rows.empty() ? "" : ", ", kind.name, kindStarted, faults);
        }
        SetEnvironmentVariableW(L"VEYRA_PUSHFLOW_DYNAMIC", nullptr);
        const uint64_t faults = pushflow::faultCount() - faultsBefore;
        const HRESULT removed = gpu.device->GetDeviceRemovedReason();
        std::printf("teardown-check %ux%u tier %u (%s): %llu sessions, %llu faults, %llu did not start, %llu runs failed, pool held %s, device 0x%08X\n",
                    w, h, tier, unguarded ? "UNGUARDED, as 3.2.0" : "guarded", static_cast<unsigned long long>(started), static_cast<unsigned long long>(faults),
                    static_cast<unsigned long long>(notStarted), static_cast<unsigned long long>(runFailures), poolHeld ? "yes" : "no", unsigned(removed));
        std::ofstream(out) << std::format("{{\"tool\": \"PushFlowBench\", \"probe\": \"teardown\", \"guarded\": {}, \"cycles\": {}, \"started\": {}, \"faults\": {}, \"notStarted\": {}, "
            "\"runFailures\": {}, \"poolHeld\": {}, \"deviceState\": {}, \"kinds\": [{}]}}\n", unguarded ? "false" : "true", teardownCheck, started, faults, notStarted,
            runFailures, poolHeld && started ? "true" : "false", unsigned(removed), rows);
        return 0;
    }
    if (leakCheck) {
        // --leak-check N: N queued inferences with no full wait at all (VEYRA_PUSHFLOW_SYNC_EVERY=0), and
        // this process's committed memory after the first quarter and at the end. On the software
        // renderer graphics memory is process memory, so work the provider never released shows here.
        const uint32_t w = sizes.empty() ? 256 : sizes[0].first, h = sizes.empty() ? 160 : sizes[0].second, tier = tiers.empty() ? 0 : tiers[0];
        SetEnvironmentVariableW(L"VEYRA_PUSHFLOW_SYNC_EVERY", L"0");
        auto a = frameTexture(gpu, w, h, 0), b = frameTexture(gpu, w, h, 8);
        auto output = pipeline::makeTexture(gpu.device.Get(), w, h, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        pushflow::PushFlowBackend flow;
        if (!a || !b || !output || !flow.initialize(gpu.device.Get(), gpu.queue.Get(), runtimeDir, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, pushflow::Encoding::Encoded, tier)) {
            std::printf("leak-check: PushFlow did not start\n"); return 1;
        }
        const auto committed = [] {
            PROCESS_MEMORY_COUNTERS_EX pmc{}; pmc.cb = sizeof(pmc);
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
            return double(pmc.PrivateUsage) / 1048576.0;
        };
        bool outputState = false;
        double early = 0;
        const int quarter = leakCheck / 4;
        for (int i = 0; i < leakCheck; ++i) {
            if (!gpu.begin() || !flow.pack(gpu.list.Get(), a.Get(), b.Get(), 0.5f, false) || !gpu.submit() || !flow.infer() || !gpu.begin()) { std::printf("leak-check: run %d failed\n", i); return 1; }
            D3D12_RESOURCE_BARRIER bar{}; bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; bar.Transition.pResource = output.Get();
            bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            if (!outputState) { bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON; bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST; gpu.list->ResourceBarrier(1, &bar); outputState = true; }
            if (!flow.resolve(gpu.list.Get(), a.Get(), b.Get(), output.Get()) || !gpu.submit() || !gpu.wait()) { std::printf("leak-check: run %d failed\n", i); return 1; }
            if (i + 1 == quarter) early = committed();
        }
        const double late = committed();
        const double perInference = (late - early) * 1024.0 / double(leakCheck - quarter);
        std::printf("leak-check %ux%u tier %u, %d queued inferences without a full wait: committed %.1f MB after %d, %.1f MB at the end (%+.1f KB per inference) -> %s\n",
                    w, h, tier, leakCheck, early, quarter, late, perInference, perInference > 64.0 ? "GROWS" : "steady");
        std::ofstream(out) << std::format("{{\"tool\": \"PushFlowBench\", \"probe\": \"leak\", \"inferences\": {}, \"earlyMB\": {:.1f}, \"lateMB\": {:.1f}, \"kbPerInference\": {:.1f}}}\n",
                                          leakCheck, early, late, perInference);
        return 0;
    }
    if (fp16Probe) {
        // --fp16-probe: is half precision safe and faster on this graphics card? PushFPS runs this in a
        // separate process (Settings -> PushFlow half precision -> Test), so a driver or DirectML fault
        // with the fp16 model ends this process, never PushFPS. Balanced at 1280x720: full precision
        // timed, then the fp16 model with PushFlow's own comparison against full precision.
        // (--sizes and --pairs shrink it for the software renderer in CI.)
        const uint32_t w = sizesGiven && !sizes.empty() ? sizes[0].first : 1280, h = sizesGiven && !sizes.empty() ? sizes[0].second : 720, tier = 1;
        const int runs = pairsGiven ? std::max(pairs, 4) : 20, skip = std::min(4, runs / 2);
        auto a = frameTexture(gpu, w, h, 0), b = frameTexture(gpu, w, h, 8);
        auto output = pipeline::makeTexture(gpu.device.Get(), w, h, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        if (!a || !b || !output) { std::printf("textures failed\n"); return 1; }
        const auto timeIt = [&](const wchar_t* precision, pushflow::Status* status) -> double {
            SetEnvironmentVariableW(L"VEYRA_PUSHFLOW_PRECISION", precision);
            pushflow::PushFlowBackend flow;
            if (!flow.initialize(gpu.device.Get(), gpu.queue.Get(), runtimeDir, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, pushflow::Encoding::Encoded, tier)) return -1;
            bool outputState = false;
            Samples network;
            for (int i = 0; i < runs; ++i) {
                if (!gpu.begin()) return -1;
                gpu.stamp(0);
                if (!flow.pack(gpu.list.Get(), a.Get(), b.Get(), 0.5f, false)) return -1;
                gpu.stamp(1);
                if (!gpu.submit() || !flow.infer() || !gpu.begin()) return -1;
                gpu.stamp(2);
                D3D12_RESOURCE_BARRIER bar{}; bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; bar.Transition.pResource = output.Get();
                bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                if (!outputState) { bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON; bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST; gpu.list->ResourceBarrier(1, &bar); outputState = true; }
                if (!flow.resolve(gpu.list.Get(), a.Get(), b.Get(), output.Get())) return -1;
                gpu.list->ResolveQueryData(gpu.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 3, gpu.readback.Get(), 0);
                if (!gpu.submit() || !gpu.wait()) return -1;
                uint64_t* ts = nullptr;
                gpu.readback->Map(0, nullptr, reinterpret_cast<void**>(&ts));
                if (i >= skip) network.add(double(ts[2] - ts[1]) * 1000.0 / double(gpu.frequency));
                const D3D12_RANGE none{0, 0}; gpu.readback->Unmap(0, &none);
            }
            if (status) *status = flow.status();
            return network.pct(0.5);
        };
        const double fullMs = timeIt(nullptr, nullptr);
        pushflow::Status half;
        const double halfMs = fullMs > 0 ? timeIt(L"fp16-checked", &half) : -1;
        const HRESULT removed = gpu.device->GetDeviceRemovedReason();
        // Half precision has to match (PushFlow's own 38 dB check) and save at least 10%.
        const bool matches = halfMs > 0 && half.precision == "fp16" && half.selfCheckDb >= 38.0;
        const bool ok = removed == S_OK && matches && halfMs < fullMs * 0.9;
        const std::string reason = removed != S_OK ? "device-removed" : fullMs <= 0 ? "pushflow-failed" : halfMs <= 0 ? "fp16-failed"
                                 : !matches ? "fp16-mismatch" : !ok ? "not-faster" : "ok";
        std::printf("fp16 probe on %s: full %.2f ms, half %.2f ms, match %.1f dB -> %s\n", gpu.adapter.c_str(), fullMs, halfMs, half.selfCheckDb, reason.c_str());
        std::ofstream(out) << std::format("{{\"tool\": \"PushFlowBench\", \"probe\": \"fp16\", \"adapter\": \"{}\", \"fullMs\": {:.3f}, \"halfMs\": {:.3f}, "
            "\"matchDb\": {:.1f}, \"deviceState\": {}, \"ok\": {}, \"reason\": \"{}\"}}\n", gpu.adapter, fullMs, halfMs, half.selfCheckDb, unsigned(removed),
            ok ? "true" : "false", reason);
        return 0;
    }
    std::printf("PushFlowBench on %s, %d pairs per run\n", gpu.adapter.c_str(), pairs);
    std::string json = std::format("{{\n  \"tool\": \"PushFlowBench\",\n  \"adapter\": \"{}\",\n  \"vramMB\": {},\n  \"runs\": [\n", gpu.adapter, gpu.vram >> 20);
    bool firstRun = true;
    const char* tierName[] = {"Fast", "Balanced", "Quality"};
    for (const auto& [w, h] : sizes) {
        auto a = frameTexture(gpu, w, h, 0), b = frameTexture(gpu, w, h, 8);
        auto output = pipeline::makeTexture(gpu.device.Get(), w, h, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        if (!a || !b || !output) { std::printf("%ux%u: textures failed\n", w, h); continue; }
        for (uint32_t tier : tiers)
          for (int variant = 0; variant < 3; ++variant) {
            // 0: the product's settings (static shapes, queued inference); --compare-shapes adds 1
            // (dynamic shapes), --compare-sync adds 2 (a CPU wait after every inference, as before 3.2.0).
            if ((variant == 1 && !compareShapes) || (variant == 2 && !compareSync)) continue;
            SetEnvironmentVariableW(L"VEYRA_PUSHFLOW_DYNAMIC", variant == 1 ? L"1" : nullptr);
            SetEnvironmentVariableW(L"VEYRA_PUSHFLOW_SYNC", variant == 2 ? L"1" : nullptr);
            const char* shapeName = variant == 1 ? "dynamic" : variant == 2 ? "waits" : "static";
            if (const HRESULT removed = gpu.device->GetDeviceRemovedReason(); removed != S_OK) {
                std::printf("device removed before %s (0x%08X); stopping\n", tierName[tier], unsigned(removed));
                break;
            }
            pushflow::PushFlowBackend flow;
            const auto initStart = std::chrono::steady_clock::now();
            if (!flow.initialize(gpu.device.Get(), gpu.queue.Get(), runtimeDir, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, pushflow::Encoding::Encoded, tier)) {
                std::printf("%ux%u %s %s: PushFlow did not start (see logs), device 0x%08X\n", w, h, tierName[tier], shapeName, unsigned(gpu.device->GetDeviceRemovedReason()));
                continue;
            }
            const double initMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - initStart).count();
            Samples pack, timePack, network, rebuild, inferCpu, pair[3];
            bool outputState = false;   // false: COMMON, true: COPY_DEST
            const auto one = [&](float t, bool timeOnly, bool measure) {
                if (!gpu.begin()) return false;
                gpu.stamp(0);
                if (!flow.pack(gpu.list.Get(), a.Get(), b.Get(), t, timeOnly)) return false;
                gpu.stamp(1);
                if (!gpu.submit()) return false;
                if (!flow.infer()) return false;
                const double cpu = flow.lastInferCpuMs();
                if (!gpu.begin()) return false;
                gpu.stamp(2);
                D3D12_RESOURCE_BARRIER bar{}; bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; bar.Transition.pResource = output.Get();
                bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                if (!outputState) { bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON; bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST; gpu.list->ResourceBarrier(1, &bar); outputState = true; }
                if (!flow.resolve(gpu.list.Get(), a.Get(), b.Get(), output.Get())) return false;
                gpu.stamp(3);
                gpu.list->ResolveQueryData(gpu.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 4, gpu.readback.Get(), 0);
                if (!gpu.submit() || !gpu.wait()) return false;
                if (measure) {
                    uint64_t* ts = nullptr;
                    gpu.readback->Map(0, nullptr, reinterpret_cast<void**>(&ts));
                    const double k = 1000.0 / double(gpu.frequency);
                    (timeOnly ? timePack : pack).add(double(ts[1] - ts[0]) * k);
                    network.add(double(ts[2] - ts[1]) * k);
                    rebuild.add(double(ts[3] - ts[2]) * k);
                    const D3D12_RANGE none{0, 0}; gpu.readback->Unmap(0, &none);
                    inferCpu.add(cpu);
                }
                return true;
            };
            bool good = true;
            for (int i = 0; i < 5 && good; ++i) good = one(0.5f, false, false);   // warm-up
            for (int i = 0; i < pairs && good; ++i)
                for (uint32_t m = 2; m <= 4 && good; ++m) {
                    const auto start = std::chrono::steady_clock::now();
                    for (uint32_t sub = 1; sub < m && good; ++sub) good = one(float(sub) / float(m), sub > 1, true);
                    pair[m - 2].add(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
                }
            if (!good) { std::printf("%ux%u %s %s: a run failed, device 0x%08X\n", w, h, tierName[tier], shapeName, unsigned(gpu.device->GetDeviceRemovedReason())); continue; }
            const auto& st = flow.status();
            std::printf("%ux%u %-8s %-7s %-6s %s rebuild=%d  network p50 %.2f ms p95 %.2f ms  call p50 %.2f ms  pair 2X %.2f / 3X %.2f / 4X %.2f ms  warm-up %.1f ms\n", w, h, tierName[tier],
                st.staticShapes ? "static" : "dynamic", st.queued ? "queued" : "waits", st.precision.c_str(), st.rebuild ? 1 : 0, network.pct(0.5), network.pct(0.95), inferCpu.pct(0.5),
                pair[0].pct(0.5), pair[1].pct(0.5), pair[2].pct(0.5), st.warmupMs);
            json += std::format("{}    {{\"width\": {}, \"height\": {}, \"tier\": \"{}\", \"shapes\": \"{}\", \"queued\": {}, \"model\": \"{}\", \"precision\": \"{}\", \"selfCheckDb\": {:.1f}, "
                "\"rebuild\": {}, \"poolHeld\": {}, \"modelSize\": \"{}x{}\", \"initMs\": {:.0f},\n      \"packMs\": {}, \"timeOnlyPackMs\": {}, \"networkMs\": {}, \"rebuildMs\": {}, \"inferCpuMs\": {},\n"
                "      \"pairWallMs\": {{\"2x\": {}, \"3x\": {}, \"4x\": {}}}}}",
                firstRun ? "" : ",\n", w, h, tierName[tier], st.staticShapes ? "static" : "dynamic", st.queued ? "true" : "false", st.model, st.precision, st.selfCheckDb, st.rebuild ? "true" : "false", st.poolHeld ? "true" : "false", st.modelW, st.modelH, initMs,
                stats(pack), stats(timePack), stats(network), stats(rebuild), stats(inferCpu), stats(pair[0]), stats(pair[1]), stats(pair[2]));
            firstRun = false;
          }
    }
    json += "\n  ],\n  \"notes\": \"networkMs is the GPU span between the pack and rebuild passes on the queue (the network plus queue gaps); pairWallMs is CPU wall time for all generated frames of one pair.\"\n}\n";
    std::ofstream(out) << json;
    std::printf("Saved %s\n", out.c_str());
    return 0;
}
