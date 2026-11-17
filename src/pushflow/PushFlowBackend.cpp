// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFlow: RIFE through ONNX Runtime + DirectML on the graph's D3D12 queue (see PushFlowBackend.h).
#include "veyra/pushflow/PushFlowBackend.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/Log.h"

#include <windows.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <vector>

#ifdef VEYRA_HAVE_ONNXRUNTIME
#include <DirectML.h>
#include <onnxruntime_c_api.h>
#include <dml_provider_factory.h>
#endif

namespace veyra::pushflow {
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace {
constexpr uint32_t kBlock = 64;   // RIFE v4 works on 1/32-size features; 64 keeps every level whole
const wchar_t* modelFile(uint32_t tier) { return tier >= 2 ? L"pushflow-quality.onnx" : L"pushflow-fast.onnx"; }
std::string narrow(const std::wstring& w) { std::string s; for (wchar_t c : w) s.push_back(c < 128 ? char(c) : '?'); return s; }
DXGI_FORMAT viewFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
    default: return f;
    }
}
// Descriptor slots: the pack pass keeps its UAV in slot 0 and a ring of SRV pairs after it, so a
// pack recorded while the GPU still reads the previous pair's descriptors never rewrites them.
constexpr UINT kSrvRing = 8;
D3D12_CPU_DESCRIPTOR_HANDLE cpuSlot(const pipeline::ComputePass& pass, UINT slot) {
    return {pass.heap->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(slot) * pass.increment};
}
uint64_t gpuSlot(const pipeline::ComputePass& pass, UINT slot) {
    return pass.heap->GetGPUDescriptorHandleForHeapStart().ptr + uint64_t(slot) * pass.increment;
}
} // namespace

std::wstring pushFlowDirectory(const std::wstring& runtimeDir) { return (fs::path(runtimeDir) / L"pushflow").wstring(); }

bool runtimeInstalled(const std::wstring& runtimeDir, std::wstring* reason) {
#ifndef VEYRA_HAVE_ONNXRUNTIME
    (void)runtimeDir;
    if (reason) *reason = L"PushFlow 未编译进这个版本";
    return false;
#else
    const fs::path dir = pushFlowDirectory(runtimeDir);
    std::error_code ec;
    for (const wchar_t* f : {L"onnxruntime.dll", L"DirectML.dll", L"models\\pushflow-fast.onnx", L"models\\pushflow-quality.onnx"})
        if (!fs::is_regular_file(dir / f, ec)) {
            if (reason) *reason = L"缺少 PushFlow 运行组件：runtime\\pushflow\\" + std::wstring(f);
            return false;
        }
    return true;
#endif
}

struct PushFlowBackend::Impl {
    Status status;
    double inferMs = 0;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    uint32_t width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    Encoding encoding = Encoding::Encoded;
    uint32_t channels = 7;
    pipeline::ComputePass packPass, unpackPass;
    pipeline::DescriptorStager stager;
    ComPtr<ID3D12Resource> inputBuffer, outputBuffer, picture;
    pipeline::StateTracker states;
    UINT ring = 0;
#ifdef VEYRA_HAVE_ONNXRUNTIME
    HMODULE ortModule = nullptr, dmlModule = nullptr;
    const OrtApi* api = nullptr;
    const OrtDmlApi* dml = nullptr;
    OrtEnv* env = nullptr;
    OrtSession* session = nullptr;
    OrtMemoryInfo* memory = nullptr;
    OrtIoBinding* binding = nullptr;
    OrtValue* inputValue = nullptr;
    OrtValue* outputValue = nullptr;
    void* inputAllocation = nullptr;
    void* outputAllocation = nullptr;
    ComPtr<IDMLDevice> dmlDevice;
    std::string inputName, outputName;

    bool ok(OrtStatus* s, const char* what) {
        if (!s) return true;
        veyra::log::error("pushflow", std::format("{} failed: {}", what, api->GetErrorMessage(s)));
        api->ReleaseStatus(s);
        return false;
    }
    void release() {
        if (api) {
            if (binding) api->ReleaseIoBinding(binding);
            if (inputValue) api->ReleaseValue(inputValue);
            if (outputValue) api->ReleaseValue(outputValue);
            if (dml && inputAllocation) dml->FreeGPUAllocation(inputAllocation);
            if (dml && outputAllocation) dml->FreeGPUAllocation(outputAllocation);
            if (memory) api->ReleaseMemoryInfo(memory);
            if (session) api->ReleaseSession(session);
            if (env) api->ReleaseEnv(env);
        }
        binding = nullptr; inputValue = outputValue = nullptr; inputAllocation = outputAllocation = nullptr;
        memory = nullptr; session = nullptr; env = nullptr; dml = nullptr; api = nullptr;
        dmlDevice.Reset();
        // The DLLs stay loaded for the process: ONNX Runtime keeps threads and state that do not unload cleanly.
    }
#endif
    ~Impl() {
#ifdef VEYRA_HAVE_ONNXRUNTIME
        release();
#endif
    }
};

PushFlowBackend::PushFlowBackend() : p_(std::make_unique<Impl>()) {}
PushFlowBackend::~PushFlowBackend() = default;
double PushFlowBackend::lastInferCpuMs() const { return p_->inferMs; }
ID3D12Resource* PushFlowBackend::tensorBuffer(bool input) const { return input ? p_->inputBuffer.Get() : p_->outputBuffer.Get(); }
uint32_t PushFlowBackend::inputChannels() const { return p_->channels; }
const Status& PushFlowBackend::status() const { return p_->status; }
void PushFlowBackend::shutdown() { p_ = std::make_unique<Impl>(); }

bool PushFlowBackend::initialize(ID3D12Device* device, ID3D12CommandQueue* queue, const std::wstring& runtimeDir, uint32_t width, uint32_t height,
                                 DXGI_FORMAT format, Encoding encoding, uint32_t tier) {
    p_ = std::make_unique<Impl>();
    auto& p = *p_;
    std::wstring why;
    if (!device || !queue || width < 16 || height < 16 || !runtimeInstalled(runtimeDir, &why)) {
        veyra::log::error("pushflow", "runtime missing or bad arguments: " + narrow(why));
        return false;
    }
#ifndef VEYRA_HAVE_ONNXRUNTIME
    (void)format; (void)encoding; (void)tier;
    return false;
#else
    p.device = device; p.queue = queue; p.width = width; p.height = height; p.format = viewFormat(format); p.encoding = encoding;
    tier = std::min(tier, kTierCount - 1);
    const double scale = tier == 0 ? 0.5 : 1.0;
    p.status.modelW = std::max<uint32_t>(32, uint32_t(std::lround(width * scale)));
    p.status.modelH = std::max<uint32_t>(32, uint32_t(std::lround(height * scale)));
    p.status.padW = (p.status.modelW + kBlock - 1) / kBlock * kBlock;
    p.status.padH = (p.status.modelH + kBlock - 1) / kBlock * kBlock;
    const fs::path dir = pushFlowDirectory(runtimeDir);
    // DirectML first, from our folder, so ONNX Runtime and we use the same (redistributed) copy.
    p.dmlModule = LoadLibraryExW((dir / L"DirectML.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    p.ortModule = LoadLibraryExW((dir / L"onnxruntime.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!p.dmlModule || !p.ortModule) { veyra::log::error("pushflow", std::format("LoadLibrary failed error={}", GetLastError())); return false; }
    using CreateDml = HRESULT(WINAPI*)(ID3D12Device*, DML_CREATE_DEVICE_FLAGS, REFIID, void**);
    using GetBase = const OrtApiBase*(ORT_API_CALL*)();
    auto createDml = reinterpret_cast<CreateDml>(reinterpret_cast<void*>(GetProcAddress(p.dmlModule, "DMLCreateDevice")));
    auto getBase = reinterpret_cast<GetBase>(reinterpret_cast<void*>(GetProcAddress(p.ortModule, "OrtGetApiBase")));
    if (!createDml || !getBase) { veyra::log::error("pushflow", "runtime entry points missing"); return false; }
    const OrtApiBase* base = getBase();
    p.api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
    if (!p.api) { veyra::log::error("pushflow", std::format("ONNX Runtime {} does not offer API {}", base ? base->GetVersionString() : "?", ORT_API_VERSION)); return false; }
    if (FAILED(createDml(device, DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&p.dmlDevice)))) { veyra::log::error("pushflow", "DMLCreateDevice failed"); return false; }
    auto* api = p.api;
    if (!p.ok(api->GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void**>(&p.dml)), "GetExecutionProviderApi") || !p.dml) return false;
    if (!p.ok(api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "pushflow", &p.env), "CreateEnv")) return false;
    OrtSessionOptions* options = nullptr;
    if (!p.ok(api->CreateSessionOptions(&options), "CreateSessionOptions")) return false;
    bool optionsOk = p.ok(api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL), "optimization level") &&
                     p.ok(api->DisableMemPattern(options), "DisableMemPattern") &&
                     p.ok(api->SetSessionExecutionMode(options, ORT_SEQUENTIAL), "execution mode") &&
                     p.ok(p.dml->SessionOptionsAppendExecutionProvider_DML1(options, p.dmlDevice.Get(), queue), "DirectML provider");
    const fs::path model = dir / L"models" / modelFile(tier);
    const auto loadStart = std::chrono::steady_clock::now();
    if (optionsOk) optionsOk = p.ok(api->CreateSession(p.env, model.c_str(), options, &p.session), "CreateSession");
    api->ReleaseSessionOptions(options);
    if (!optionsOk) return false;
    // The model's input and output: [1, 7, H, W] -> [1, 3, H, W], float.
    OrtAllocator* allocator = nullptr;
    if (!p.ok(api->GetAllocatorWithDefaultOptions(&allocator), "allocator")) return false;
    size_t inputs = 0, outputs = 0;
    api->SessionGetInputCount(p.session, &inputs); api->SessionGetOutputCount(p.session, &outputs);
    if (inputs != 1 || outputs < 1) { veyra::log::error("pushflow", std::format("model has {} inputs and {} outputs; expected 1 and 1", inputs, outputs)); return false; }
    char* name = nullptr;
    if (!p.ok(api->SessionGetInputName(p.session, 0, allocator, &name), "input name")) return false;
    p.inputName = name; api->AllocatorFree(allocator, name);
    if (!p.ok(api->SessionGetOutputName(p.session, 0, allocator, &name), "output name")) return false;
    p.outputName = name; api->AllocatorFree(allocator, name);
    OrtTypeInfo* typeInfo = nullptr;
    if (!p.ok(api->SessionGetInputTypeInfo(p.session, 0, &typeInfo), "input type")) return false;
    const OrtTensorTypeAndShapeInfo* tensorInfo = nullptr;
    api->CastTypeInfoToTensorInfo(typeInfo, &tensorInfo);
    ONNXTensorElementDataType element = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
    size_t dims = 0;
    int64_t shape[4]{};
    if (tensorInfo) { api->GetTensorElementType(tensorInfo, &element); api->GetDimensionsCount(tensorInfo, &dims); if (dims == 4) api->GetDimensions(tensorInfo, shape, 4); }
    api->ReleaseTypeInfo(typeInfo);
    if (dims != 4 || (shape[1] != 7 && shape[1] != 11) || element != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        veyra::log::error("pushflow", std::format("model input not [N,7|11,H,W] float (dims={} channels={} type={})", dims, dims == 4 ? shape[1] : 0, int(element)));
        return false;
    }
    p.channels = uint32_t(shape[1]);
    if ((shape[2] > 0 && shape[2] != int64_t(p.status.padH)) || (shape[3] > 0 && shape[3] != int64_t(p.status.padW))) {
        veyra::log::error("pushflow", std::format("model has a fixed size {}x{}; needed {}x{}", shape[3], shape[2], p.status.padW, p.status.padH));
        return false;
    }
    // D3D12 buffers for the tensors, always in UNORDERED_ACCESS (as DirectML expects).
    const uint64_t plane = uint64_t(p.status.padW) * p.status.padH;
    const auto makeBuffer = [&](uint64_t bytes, ComPtr<ID3D12Resource>& out) {
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = (bytes + 255) / 256 * 256; bd.Height = 1;
        bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return SUCCEEDED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&out)));
    };
    if (!makeBuffer(plane * p.channels * 4, p.inputBuffer) || !makeBuffer(plane * 3 * 4, p.outputBuffer)) { veyra::log::error("pushflow", "tensor buffers"); return false; }
    if (!p.ok(api->CreateMemoryInfo("DML", OrtDeviceAllocator, 0, OrtMemTypeDefault, &p.memory), "memory info")) return false;
    if (!p.ok(p.dml->CreateGPUAllocationFromD3DResource(p.inputBuffer.Get(), &p.inputAllocation), "input allocation") ||
        !p.ok(p.dml->CreateGPUAllocationFromD3DResource(p.outputBuffer.Get(), &p.outputAllocation), "output allocation")) return false;
    const int64_t inShape[4]{1, int64_t(p.channels), int64_t(p.status.padH), int64_t(p.status.padW)};
    const int64_t outShape[4]{1, 3, int64_t(p.status.padH), int64_t(p.status.padW)};
    if (!p.ok(api->CreateTensorWithDataAsOrtValue(p.memory, p.inputAllocation, size_t(plane * p.channels * 4), inShape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &p.inputValue), "input tensor") ||
        !p.ok(api->CreateTensorWithDataAsOrtValue(p.memory, p.outputAllocation, size_t(plane * 3 * 4), outShape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &p.outputValue), "output tensor")) return false;
    if (!p.ok(api->CreateIoBinding(p.session, &p.binding), "binding") ||
        !p.ok(api->BindInput(p.binding, p.inputName.c_str(), p.inputValue), "bind input") ||
        !p.ok(api->BindOutput(p.binding, p.outputName.c_str(), p.outputValue), "bind output")) return false;
    // Shaders, descriptors and the output picture.
    std::vector<uint8_t> cs;
    if (!p.packPass.loadShader("PushFlowPack.dxil", cs) || !p.packPass.create(device, cs, 1 + 2 * kSrvRing, 2, 1, 12)) return false;
    if (!p.unpackPass.loadShader("PushFlowUnpack.dxil", cs) || !p.unpackPass.create(device, cs, 3, 1, 2, 8)) return false;
    if (!p.stager.initialize(device, 2 * kSrvRing)) return false;
    p.picture = pipeline::makeTexture(device, width, height, p.format, true);
    if (!p.picture) { veyra::log::error("pushflow", "output picture"); return false; }
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_UNKNOWN; uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = UINT(plane * p.channels); uav.Buffer.StructureByteStride = 4;
    device->CreateUnorderedAccessView(p.inputBuffer.Get(), nullptr, &uav, cpuSlot(p.packPass, 0));
    uav.Buffer.NumElements = UINT(plane * 3);
    device->CreateUnorderedAccessView(p.outputBuffer.Get(), nullptr, &uav, cpuSlot(p.unpackPass, 1));
    pipeline::makeUav(device, p.picture.Get(), p.format, cpuSlot(p.unpackPass, 2));
    p.status.provider = "DirectML";
    p.status.model = narrow(modelFile(tier));
    p.status.ready = true;
    veyra::log::info("pushflow", std::format("ready runtime={} model={} tier={} size={}x{} model={}x{} padded={}x{} channels={} encoding={} loadMs={:.0f}",
        base->GetVersionString(), p.status.model, tier, width, height, p.status.modelW, p.status.modelH, p.status.padW, p.status.padH,
        p.channels, unsigned(encoding), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - loadStart).count()));
    return true;
#endif
}

bool PushFlowBackend::pack(ID3D12GraphicsCommandList* list, ID3D12Resource* previous, ID3D12Resource* current, float timestep) {
    auto& p = *p_;
    if (!p.status.ready || !list || !previous || !current) return false;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = p.format; view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; view.Texture2D.MipLevels = 1;
    const UINT first = 1 + 2 * p.ring;
    p.ring = (p.ring + 1) % kSrvRing;
    p.stager.stageSrv(previous, &view, p.packPass.heap.Get(), first);
    p.stager.stageSrv(current, &view, p.packPass.heap.Get(), first + 1);
    const float constants[12] = {std::bit_cast<float>(p.width), std::bit_cast<float>(p.height), std::bit_cast<float>(p.status.padW), std::bit_cast<float>(p.status.padH),
                                 std::bit_cast<float>(p.status.modelW), std::bit_cast<float>(p.status.modelH), std::bit_cast<float>(uint32_t(p.encoding)), std::bit_cast<float>(p.channels),
                                 std::clamp(timestep, 0.0f, 1.0f), 0.0f, 0.0f, 0.0f};
    p.packPass.bind(list, constants, gpuSlot(p.packPass, first), gpuSlot(p.packPass, 0));
    list->Dispatch((p.status.padW + 15) / 16, (p.status.padH + 15) / 16, 1);
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; barrier.UAV.pResource = p.inputBuffer.Get();
    list->ResourceBarrier(1, &barrier);
    return true;
}

bool PushFlowBackend::infer() {
    auto& p = *p_;
    if (!p.status.ready) return false;
#ifdef VEYRA_HAVE_ONNXRUNTIME
    const auto start = std::chrono::steady_clock::now();
    // DirectML records the network into its own command lists and may hold them back; synchronizing the
    // bound outputs submits them to the queue and waits, so resolve() reads the finished output.
    const bool done = p.ok(p.api->RunWithBinding(p.session, nullptr, p.binding), "RunWithBinding") &&
                      p.ok(p.api->SynchronizeBoundOutputs(p.binding), "SynchronizeBoundOutputs");
    p.inferMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return done;
#else
    return false;
#endif
}

bool PushFlowBackend::resolve(ID3D12GraphicsCommandList* list, ID3D12Resource* output) {
    auto& p = *p_;
    if (!p.status.ready || !list || !output) return false;
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; barrier.UAV.pResource = p.outputBuffer.Get();
    list->ResourceBarrier(1, &barrier);
    p.states.transition(list, p.picture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const float constants[8] = {std::bit_cast<float>(p.width), std::bit_cast<float>(p.height), std::bit_cast<float>(p.status.padW), std::bit_cast<float>(p.status.padH),
                                std::bit_cast<float>(p.status.modelW), std::bit_cast<float>(p.status.modelH), std::bit_cast<float>(uint32_t(p.encoding)), 0.0f};
    p.unpackPass.bind(list, constants, gpuSlot(p.unpackPass, 0), gpuSlot(p.unpackPass, 1));
    list->Dispatch((p.width + 15) / 16, (p.height + 15) / 16, 1);
    p.states.transition(list, p.picture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyResource(output, p.picture.Get());
    return true;
}

} // namespace veyra::pushflow
