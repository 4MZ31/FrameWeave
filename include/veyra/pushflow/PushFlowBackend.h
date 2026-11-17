// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFlow: PushFPS's own frame generation. A RIFE network (Real-Time Intermediate Flow Estimation,
// MIT-licensed weights by hzwer, ONNX from vs-mlrt) makes the frame between two real frames at any
// time, run through ONNX Runtime on DirectML, so it works on every DirectX 12 graphics card
// (NVIDIA, AMD, Intel). Everything stays on the GPU: a compute pass packs the two frames into the
// network's input buffer, ONNX Runtime runs the network on the graph's own command queue (so no CPU
// waits or copies), and a second pass turns the output back into a picture. Speed-ups over a plain
// RIFE run: half-precision weights (converted when the release is built), I/O bound straight to
// D3D12 buffers, padding only to the network's block size, and a reduced model size in the faster
// tiers. The runtime (onnxruntime.dll, DirectML.dll, the models) is in runtime\pushflow; without it
// PushFlow reports itself unavailable. Tiers (EnhancementSettings::vfgQuality, shared with VFG):
//   0 Fast: the lite model at 50 % size;  1 Balanced: the lite model at full size;
//   2 Quality: the full model at full size.
#pragma once
#include <d3d12.h>
#include <cstdint>
#include <memory>
#include <string>

namespace veyra::pushflow {

constexpr uint32_t kTierCount = 3;
// The pictures' encoding for the network (PushFlowPack.hlsl).
enum class Encoding : uint32_t { Encoded = 0, LinearSdr = 1, ScRgbHdr = 2 };

struct Status {
    bool ready = false;
    std::string provider;          // "DirectML"
    std::string model;             // file name of the model in use
    uint32_t modelW = 0, modelH = 0, padW = 0, padH = 0;
};

// Whether runtime\pushflow has the files PushFlow needs (file check only).
bool runtimeInstalled(const std::wstring& runtimeDir, std::wstring* reason = nullptr);
// runtimeDir\pushflow
std::wstring pushFlowDirectory(const std::wstring& runtimeDir);

class PushFlowBackend {
public:
    PushFlowBackend();
    ~PushFlowBackend();
    PushFlowBackend(const PushFlowBackend&) = delete;
    PushFlowBackend& operator=(const PushFlowBackend&) = delete;

    // `queue` is the queue every list below is submitted to; ONNX Runtime runs the network on it too.
    bool initialize(ID3D12Device* device, ID3D12CommandQueue* queue, const std::wstring& runtimeDir, uint32_t width, uint32_t height,
                    DXGI_FORMAT format, Encoding encoding, uint32_t tier);
    // Records the input packing: `previous` and `current` must be in NON_PIXEL_SHADER_RESOURCE.
    bool pack(ID3D12GraphicsCommandList* list, ID3D12Resource* previous, ID3D12Resource* current, float timestep);
    // After the list holding pack() was submitted to the queue: runs the network (submits to the queue).
    bool infer();
    // Records the output: into an internal texture, then copied to `output` (left in COPY_DEST).
    bool resolve(ID3D12GraphicsCommandList* list, ID3D12Resource* output);
    double lastInferCpuMs() const;
    const Status& status() const;
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace veyra::pushflow
