// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS ground-truth frame-generation quality test: withholding, readback and the report.
#include "veyra/engine/PushQualityProbe.h"
#include "veyra/Log.h"
#include "veyra/engine/PushWriteCombined.h"
#include "veyra/pipeline/CaptureUploadFrame.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace veyra::engine {
namespace {
constexpr size_t kMaxRefs = 24, kMaxKept = 32;
constexpr int64_t kMatch100ns = 20000;          // 2 ms: same moment
constexpr uint32_t kRealTarget = 6;             // real frames for the tone fit
}

void PushQualityProbe::start(const Options& options) {
    std::lock_guard lock(m_);
    options_ = options;
    options_.seconds = std::clamp<uint32_t>(options.seconds, 2, 30);
    options_.targetSamples = std::clamp<uint32_t>(options.targetSamples, 4, 120);
    done_ = failed_ = false; error_.clear();
    refs_.clear(); kept_.clear(); lastKept_ = {}; generated_.clear(); real_.clear(); report_ = {};
    toggle_ = false; withheld_ = 0; lastPts_ = 0; shown_ = onTime_ = 0; copyLogs_ = 0;
    started_ = std::chrono::steady_clock::now(); nextReal_ = started_;
    ++revision_;
    running_ = true;
    veyra::log::info("push-quality", std::format("ground-truth test started seconds={} target={} hudBoxes={}", options_.seconds,
                                                 options_.targetSamples, options_.hud.size()));
}

void PushQualityProbe::cancel() {
    std::lock_guard lock(m_);
    if (running_) veyra::log::info("push-quality", std::format("cancelled after {} samples", generated_.size()));
    running_ = false;
}

void PushQualityProbe::stopLocked(const wchar_t* error) {
    running_ = false; failed_ = true; error_ = error;
}

bool PushQualityProbe::withhold(const AVFrame* frame, int64_t pts100ns, bool packedCarrier) {
    if (!running_.load(std::memory_order_relaxed) || !frame) return false;
    quality::SourcePlanes src;
    src.plane0 = frame->data[0]; src.pitch0 = size_t(std::max(0, frame->linesize[0]));
    src.width = uint32_t(std::max(0, frame->width)); src.height = uint32_t(std::max(0, frame->height));
    src.limitedRange = frame->color_range != AVCOL_RANGE_JPEG;
    bool supported = !packedCarrier;
    switch (frame->format) {
    case AV_PIX_FMT_NV12: src.format = quality::SourceFormat::Nv12; break;
    case AV_PIX_FMT_P010LE: src.format = quality::SourceFormat::P010; break;
    case AV_PIX_FMT_YUYV422: src.format = quality::SourceFormat::Yuy2; break;
    case AV_PIX_FMT_YUV420P: src.format = quality::SourceFormat::Yuv420p; break;
    case AV_PIX_FMT_BGRA: case AV_PIX_FMT_BGR0: src.format = quality::SourceFormat::Bgra8; break;
    case AV_PIX_FMT_RGBA: case AV_PIX_FMT_RGB0: src.format = quality::SourceFormat::Rgba8; break;
    default: supported = false; break;
    }
    // A capture frame whose pixels are in the D3D12 upload buffer (CaptureUploadFrame.h): only the rows the
    // luma grid reads are copied out, with streaming loads, before anything looks at them. Reading the upload
    // buffer byte by byte took the engine thread a large part of a second a frame (4.5.3, issue 15: the
    // output fell to about 4 FPS while the test ran, and frame generation never made a frame to compare).
    const auto* upload = pipeline::captureUploadFrame(frame);
    if (supported && upload && upload->direct && src.plane0 && src.height >= 16 && src.pitch0 > 0) {
        const auto copyStart = std::chrono::steady_clock::now();
        const size_t pitch = src.pitch0;
        const size_t bpp = src.format == quality::SourceFormat::P010 || src.format == quality::SourceFormat::Yuy2 ? 2
            : src.format == quality::SourceFormat::Bgra8 || src.format == quality::SourceFormat::Rgba8 ? 4 : 1;
        const size_t rowBytes = std::min(pitch, (size_t(src.width) * bpp + 63) & ~size_t(63));
        if (scratch_.size() < pitch * src.height) scratch_.assign(pitch * src.height, 0);
        uint32_t last = UINT32_MAX;
        for (uint32_t gy = 0; gy < quality::kGridH; ++gy)
            for (int sy = 0; sy < quality::kGridTaps; ++sy) {
                const uint32_t row = quality::gridSourceRow(gy, sy, src.height);
                if (row == last) continue;
                last = row;
                copyFromWriteCombined(scratch_.data() + size_t(row) * pitch, src.plane0 + size_t(row) * pitch, rowBytes);
            }
        src.plane0 = scratch_.data();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - copyStart).count();
        if (copyLogs_.fetch_add(1, std::memory_order_relaxed) < 3) {
            veyra::log::info("push-quality", std::format("upload-buffer rows copied in {:.2f} ms ({}x{}, {} bytes a row)", ms, src.width, src.height, rowBytes));
        }
    }
    // Worked out before the lock: the presenter's record() takes the same lock for every frame it shows.
    quality::Luma luma;
    const bool lumaOk = supported && quality::lumaFromSource(src, luma);
    std::lock_guard lock(m_);
    if (!running_) return false;
    if (std::chrono::steady_clock::now() - started_ > std::chrono::seconds(options_.seconds)) { finishLocked(); return false; }
    if (!supported) {
        veyra::log::warn("push-quality", std::format("source pixels not readable on the CPU (format {} packed={})", frame->format, packedCarrier));
        stopLocked(L"这个画面来源不支持画质测试（画面不在内存里）；请用采集卡或画面输入接口");
        return false;
    }
    if (!lumaOk) { stopLocked(L"无法读取画面，画质测试已停止"); return false; }
    toggle_ = !toggle_;
    if (!toggle_) {
        // Kept back: frame generation makes this moment.
        Ref ref; ref.pts = pts100ns; ref.truth = std::move(luma); ref.previous = lastKept_;
        refs_.push_back(std::move(ref));
        while (refs_.size() > kMaxRefs) refs_.pop_front();
        ++withheld_;
        return true;
    }
    for (auto& r : refs_) if (!r.haveNext && r.pts < pts100ns) { r.next = luma; r.haveNext = true; }
    lastKept_ = luma;
    kept_.push_back(Kept{pts100ns, std::move(luma), false});
    while (kept_.size() > kMaxKept) kept_.pop_front();
    return false;
}

bool PushQualityProbe::layoutFor(DXGI_FORMAT format, quality::OutputLayout& layout) const {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        layout = quality::OutputLayout::Rgba8; return true;
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8X8_UNORM:
        layout = quality::OutputLayout::Bgra8; return true;
    case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        layout = quality::OutputLayout::Rgb10a2; return true;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        layout = quality::OutputLayout::Rgba16fLinear; return true;
    default: return false;
    }
}

bool PushQualityProbe::record(ID3D12GraphicsCommandList* list, const Frame& frame) {
    if (!running_.load(std::memory_order_relaxed)) return false;
    std::lock_guard lock(m_);
    if (!running_ || !list || !frame.texture) return false;
    const auto now = std::chrono::steady_clock::now();
    // Timing of every shown frame (generated ones counted).
    if (frame.pts100ns > 0) {
        if (lastPts_ > 0 && frame.generated) {
            ++shown_;
            const int64_t host = std::chrono::duration_cast<std::chrono::nanoseconds>(now - lastHost_).count() / 100;
            if (quality::onTime(frame.pts100ns - lastPts_, host)) ++onTime_;
        }
        lastPts_ = frame.pts100ns; lastHost_ = now;
    }
    if (pending_ || inFlight_ || reading_ || frame.pts100ns <= 0) return false;
    if (frame.hdrMode != 0) { stopLocked(L"画质测试需要 SDR 画面：请先关闭 HDR 输出"); return false; }
    // What to read: a generated frame at a withheld moment, or now and then a real frame for the fit.
    bool want = false;
    if (frame.generated) {
        for (auto& r : refs_)
            if (!r.used && r.haveNext && std::llabs(r.pts - frame.pts100ns) <= kMatch100ns) { want = true; break; }
    } else if (real_.size() < kRealTarget && now >= nextReal_) {
        for (auto& k : kept_)
            if (!k.used && std::llabs(k.pts - frame.pts100ns) <= kMatch100ns) { want = true; break; }
    }
    if (!want) return false;
    quality::OutputLayout layout;
    if (!layoutFor(frame.format, layout)) { stopLocked(L"当前画面格式不支持画质测试"); return false; }
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    if (FAILED(frame.texture->GetDevice(IID_PPV_ARGS(&device)))) return false;
    const D3D12_RESOURCE_DESC desc = frame.texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0; UINT64 rowBytes = 0, total = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowBytes, &total);
    if (!total) return false;
    if (!readback_ || device_.Get() != device.Get() || readbackBytes_ < total) {
        readback_.Reset();
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1;
        bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback_)))) {
            stopLocked(L"显存不足，画质测试已停止");
            return false;
        }
        device_ = device; readbackBytes_ = total;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = frame.texture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = frame.texture; from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; from.SubresourceIndex = 0;
    to.pResource = readback_.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint = footprint;
    list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
    footprint_ = footprint;
    width_ = std::min<uint32_t>(frame.width ? frame.width : uint32_t(desc.Width), uint32_t(desc.Width));
    height_ = std::min<uint32_t>(frame.height ? frame.height : desc.Height, desc.Height);
    layout_ = layout;
    pendingMeta_ = Pending{frame.generated, frame.pts100ns};
    pending_ = true;
    return true;
}

void PushQualityProbe::submitted(ID3D12Fence* fence, uint64_t value) {
    std::lock_guard lock(m_);
    if (!pending_) return;
    pending_ = false;
    fence_ = fence; fenceValue_ = value; inFlight_ = fence != nullptr; flightMeta_ = pendingMeta_;
}

void PushQualityProbe::discarded() {
    std::lock_guard lock(m_);
    pending_ = false;
}

void PushQualityProbe::finishLocked() {
    running_ = false;
    std::vector<const quality::Luma*> src, out;
    for (const auto& p : real_) { src.push_back(&p.source); out.push_back(&p.presented); }
    auto fit = quality::fitLinear(src, out);
    if (!fit.valid) fit = quality::Fit{1, 0, true};   // too few real frames: compare as they are (said in the log)
    double realMse = 0;
    for (const auto& p : real_) realMse += std::pow(10.0, -quality::psnr(quality::apply(fit, p.source), p.presented) / 10.0);
    const double realPsnr = real_.empty() ? 0 : std::min(100.0, -10.0 * std::log10(realMse / double(real_.size())));
    std::vector<quality::Sample> samples;
    for (const auto& g : generated_) samples.push_back(quality::measure(g.generated, g.ref.truth, g.ref.previous, g.ref.next, fit, options_.hud));
    report_ = quality::summarize(samples, realPsnr, uint32_t(real_.size()), shown_, onTime_);
    if (samples.empty()) {
        failed_ = true;
        error_ = withheld_ == 0 ? L"没有收到画面，无法测试画质" : L"没有生成的帧可以比较：请先打开补帧（2 倍或以上）";
    } else done_ = true;
    generated_.clear(); real_.clear(); refs_.clear(); kept_.clear();
    // A copy still in flight keeps its buffer; the next start reuses it.
    if (!pending_ && !inFlight_ && !reading_) { readback_.Reset(); device_.Reset(); readbackBytes_ = 0; }
    veyra::log::info("push-quality", std::format(
        "done samples={} psnr={:.2f} blendPsnr={:.2f} gain={:.2f} realPsnr={:.2f} realSamples={} hudPsnr={:.2f} hudSamples={} meanError={:.4f} bad={:.4f} onTime={}/{} fitGain={:.3f} fitOffset={:.3f} withheld={} score={:.1f}",
        report_.samples, report_.psnr, report_.blendPsnr, report_.gainOverBlend(), report_.realPsnr, report_.realSamples, report_.hudPsnr,
        report_.hudSamples, report_.meanError, report_.badShare, report_.generatedOnTime, report_.generatedShown, fit.gain, fit.offset, withheld_,
        report_.score()));
}

PushQualityProbe::Status PushQualityProbe::status() {
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    uint32_t width = 0, height = 0;
    quality::OutputLayout layout{};
    Pending meta;
    uint64_t revision = 0;
    {
        std::lock_guard lock(m_);
        if (inFlight_ && fence_ && fence_->GetCompletedValue() >= fenceValue_) {
            inFlight_ = false; fence_.Reset();
            if (running_) { buffer = readback_; footprint = footprint_; width = width_; height = height_; layout = layout_; meta = flightMeta_; revision = revision_; reading_ = true; }
        }
    }
    if (buffer) {
        quality::Luma luma;
        bool ok = false;
        void* mapped = nullptr;
        const D3D12_RANGE range{0, SIZE_T(footprint.Offset + uint64_t(footprint.Footprint.RowPitch) * height)};
        if (SUCCEEDED(buffer->Map(0, &range, &mapped)) && mapped) {
            ok = quality::lumaFromOutput(static_cast<const uint8_t*>(mapped) + footprint.Offset, footprint.Footprint.RowPitch, width, height, layout, luma);
            const D3D12_RANGE none{0, 0};
            buffer->Unmap(0, &none);
        }
        std::lock_guard lock(m_);
        reading_ = false;
        if (running_ && revision == revision_ && ok) {
            if (meta.generated) {
                for (auto& r : refs_)
                    if (!r.used && r.haveNext && std::llabs(r.pts - meta.pts) <= kMatch100ns) { r.used = true; generated_.push_back(GenPair{std::move(luma), r}); break; }
            } else {
                for (auto& k : kept_)
                    if (!k.used && std::llabs(k.pts - meta.pts) <= kMatch100ns) { k.used = true; real_.push_back(RealPair{std::move(luma), k.luma}); break; }
                nextReal_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
            }
            if (generated_.size() >= options_.targetSamples && real_.size() >= 3) finishLocked();
        }
    }
    std::lock_guard lock(m_);
    if (running_ && std::chrono::steady_clock::now() - started_ > std::chrono::seconds(options_.seconds + 3)) finishLocked();
    Status out;
    out.running = running_; out.done = done_; out.failed = failed_; out.error = error_;
    out.samples = uint32_t(generated_.size()); out.target = options_.targetSamples; out.withheld = withheld_;
    out.revision = revision_;
    if (done_) out.report = report_;
    return out;
}

} // namespace veyra::engine
