// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFlow: PushFPS's own frame generation. A RIFE network (Real-Time Intermediate Flow Estimation,
// MIT-licensed weights by hzwer, ONNX from vs-mlrt) makes the frame between two real frames at any
// time, run through ONNX Runtime on DirectML, so it works on every DirectX 12 graphics card
// (NVIDIA, AMD, Intel). Everything stays on the GPU: a compute pass packs the two frames into the
// network's input buffer, ONNX Runtime runs the network on the graph's own command queue, and a second
// pass turns the output back into a picture.
//
// Quality and speed (docs/PUSHFLOW.md):
//  - The models also output RIFE's final warp grids and blend mask (scripts/pushflow/rife_graph.py).
//    In the Fast tier the network runs at half size and PushFlowReconstruct.hlsl enlarges the motion,
//    not the picture: every pixel is sampled from the full-resolution real frames, so detail survives.
//  - A pair is packed once; the 2nd/3rd subframe of 3X/4X only rewrites the time plane.
//  - Half-precision models (fp16 convolutions, fp32 warps/resizes/blend) are used only when a self-check
//    on this graphics card at start-up matches the full-precision model; otherwise fp32. The environment
//    variable VEYRA_PUSHFLOW_PRECISION=fp32|fp16 forces one (fp16 still needs its file).
//  - Per-frame inference times are kept in a small ring (telemetry()) and summarised in the log.
// Tiers (EnhancementSettings::vfgQuality, shared with VFG):
//   0 Fast: the lite model, motion at up to 960 x 540 (and at most half size);
//   1 Balanced: the lite model, motion at up to 1280 x 720;  2 Quality: the full model, motion at up to 1920 x 1080.
// Whenever the network runs smaller than the picture, the picture is rebuilt at full size from the
// full-resolution real frames (grids + mask, PushFlowReconstruct.hlsl). Running RIFE at 4K made every
// tier too slow for live play (field report 2026-11-17: capped at the game's 30 FPS, the optimiser saw
// no generated frames): the admission control skipped every pair it could not finish in time.
#pragma once
#include <d3d12.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

namespace veyra::pushflow {

constexpr uint32_t kTierCount = 3;
// Crash guard (field report 2026-11-17: an access violation inside onnxruntime.dll on every connect,
// RTX 5090, with PushFlow left selected): a marker file is written before ONNX Runtime is touched and
// removed once PushFlow has run steadily (or was closed normally). If it is still there at the next
// start, PushFPS died inside PushFlow, and the interface switches to another method instead of
// crashing again.
std::wstring crashMarkerPath();
bool crashedLastTime();
void clearCrashMarker();
// The network's share of the picture's width and height for a tier (pure, unit-tested).
inline double networkScale(uint32_t tier, uint32_t width, uint32_t height) {
    static constexpr double kMaxPixels[kTierCount] = {960.0 * 540.0, 1280.0 * 720.0, 1920.0 * 1080.0};
    const double area = std::max(1.0, double(width) * double(height));
    const double cap = std::sqrt(kMaxPixels[std::min(tier, kTierCount - 1)] / area);
    return std::min(tier == 0 ? 0.5 : 1.0, cap);
}
// The pictures' encoding for the network (PushFlowPack.hlsl).
enum class Encoding : uint32_t { Encoded = 0, LinearSdr = 1, ScRgbHdr = 2 };

struct Status {
    bool ready = false;
    bool rebuild = false;          // the full-resolution rebuild from grids and mask is used
    bool staticShapes = false;     // the session's input size is fixed (free dimensions overridden)
    bool queued = false;           // inference returns without waiting for the GPU (VEYRA_PUSHFLOW_SYNC=1: waits)
    double warmupMs = 0;           // the second warm-up run at start (network and upload, CPU wall time)
    bool poolHeld = false;         // the provider's GPU buffer pool is kept alive until the session is released
    std::string provider;          // "DirectML"
    std::string model;             // file name of the model in use
    std::string precision;         // "fp32" or "fp16"
    double selfCheckDb = 0;        // fp16 against fp32 at start-up (0 = not run)
    uint32_t modelW = 0, modelH = 0, padW = 0, padH = 0;
    // The model's grids and mask and how they are read (its <model>.onnx.txt; pushflow/PushFlowMotion.h).
    bool grids = false, alignCorners = true, maskForFirst = true;
};

struct Telemetry {
    uint64_t inferences = 0, fullPacks = 0, timePacks = 0;
    double inferP50Ms = 0, inferP95Ms = 0, inferMaxMs = 0;   // the network's GPU span on the queue (CPU time of the call without timestamps)
};

// ONNX Runtime faults caught since the process started (each one turned PushFlow off for its session).
uint64_t faultCount();
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
    // timeOnly: the same pair as the last call (later subframes): only the time plane is written.
    bool pack(ID3D12GraphicsCommandList* list, ID3D12Resource* previous, ID3D12Resource* current, float timestep, bool timeOnly = false);
    // After the list holding pack() was submitted to the queue: runs the network (submits to the queue).
    bool infer();
    // Records the output into an internal picture, then copies it to `output` (left in COPY_DEST).
    // `previous` / `current` (the same pair, NON_PIXEL_SHADER_RESOURCE) feed the full-resolution rebuild.
    bool resolve(ID3D12GraphicsCommandList* list, ID3D12Resource* previous, ID3D12Resource* current, ID3D12Resource* output);
    double lastInferCpuMs() const;
    Telemetry telemetry() const;
    // Tests: the network's input buffer and RGB output buffer (UNORDERED_ACCESS, planar float).
    ID3D12Resource* tensorBuffer(bool input) const;
    // Tests: the last inference's grid0 (0), grid1 (1) and mask (2), planar float in UNORDERED_ACCESS
    // (pushflow/PushFlowMotion.h says what they mean); null for a model without them.
    ID3D12Resource* motionBuffer(uint32_t index) const;
    uint32_t inputChannels() const;
    const Status& status() const;
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace veyra::pushflow
