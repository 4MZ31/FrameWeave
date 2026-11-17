// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS automatic Remote Play latency test: reads presented frames around a press (PushInputLatency.h).
#pragma once
#include "veyra/engine/PresentCapture.h"
#include "veyra/engine/PushInputLatency.h"

#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <mutex>

namespace veyra::engine {

class PushInputLatencyProbe final : public PresentCapture {
public:
    // Starts reading presented frames (a window around one press).
    void beginWindow();
    void endWindow();
    // The clock used for both ends (steady clock, 100 ns).
    static int64_t now100ns();
    // The press went out at sentHost100ns (now100ns()).
    void markSent(int64_t sentHost100ns);
    // Polled by the UI: reads finished copies. Returns press-to-picture in ms once seen, else -1.
    double poll();
    bool windowOpen() const { std::lock_guard lock(m_); return open_; }

    bool record(ID3D12GraphicsCommandList* list, const Frame& frame) override;
    void submitted(ID3D12Fence* fence, uint64_t value) override;
    void discarded() override;

private:
    struct Slot {
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        uint64_t fenceValue = 0, bytes = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        uint32_t width = 0, height = 0;
        quality::OutputLayout layout{};
        int64_t host100ns = 0;
        uint64_t order = 0;
        enum class State { Free, Pending, InFlight } state = State::Free;
    };
    mutable std::mutex m_;
    bool open_ = false;
    int64_t sent_ = 0;
    double answerMs_ = -1;
    uint64_t order_ = 0, lastRead_ = 0;
    latency::Detector detector_;
    std::array<Slot, 4> slots_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
};

} // namespace veyra::engine
