// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS Frame Input API: the live source (frames from another application as BGRA AVFrames).
#include "veyra/source/FrameInputSource.h"
#include "veyra/source/PushOpaqueRow.h"
#include "veyra/Log.h"

#include <windows.h>
#include <chrono>
#include <cstring>
#include <format>
#include <mutex>
#include <string>

extern "C" {
#include <libavutil/frame.h>
}

namespace veyra::source {
namespace {
int64_t host100ns() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 100; }
std::string narrow(const std::wstring& w) {   // channel names are ASCII (validFrameInputChannel)
    std::string out;
    for (wchar_t c : w) out.push_back(c < 128 ? static_cast<char>(c) : '?');
    return out;
}
std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
    std::wstring out(size_t(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), out.data(), n);
    return out;
}
int64_t qpc100ns() {
    LARGE_INTEGER now{}, freq{};
    QueryPerformanceCounter(&now); QueryPerformanceFrequency(&freq);
    return int64_t(static_cast<long double>(now.QuadPart) * 10000000.0L / freq.QuadPart);
}
} // namespace

struct FrameInputSource::Impl {
    FrameInputChannel channel;
    std::wstring name;
    SourceInfo info;
    AVFrame* frame = nullptr;
    FrameInputFrame latest;
    uint64_t sequence = 0;
    int64_t lastPts = 0, clockOffset = 0;
    ULONGLONG lastFrameTick = 0;
    bool waiting = false;
    mutable std::mutex messageMutex;
    std::wstring message;
    void say(std::wstring m) { std::lock_guard lock(messageMutex); message = std::move(m); }
};

FrameInputSource::FrameInputSource() : p_(std::make_unique<Impl>()) {}
FrameInputSource::~FrameInputSource() { close(); }
const SourceInfo& FrameInputSource::info() const { return p_->info; }
std::wstring FrameInputSource::status() const { std::lock_guard lock(p_->messageMutex); return p_->message; }

bool FrameInputSource::open(const SourceOpenDesc& desc) {
    close();
    auto& p = *p_;
    if (!parseFrameInputUri(desc.path, p.name)) { p.say(L"画面输入通道名称无效"); return false; }
    if (!p.channel.open(p.name)) { p.say(std::format(L"没有应用在通道「{}」发送画面", p.name)); return false; }
    // The first frame gives the size; wait briefly for it.
    if (!p.channel.readNewest(p.latest, 2000)) {
        p.say(std::format(L"通道「{}」已打开，但还没有收到画面", p.name));
        p.channel.close();
        return false;
    }
    const auto sender = p.channel.sender();
    p.frame = av_frame_alloc();
    if (!p.frame) { p.channel.close(); return false; }
    p.clockOffset = host100ns() - qpc100ns();
    auto& i = p.info;
    i = {};
    i.opened = true;
    i.kind = pipeline::SourceKind::FrameInput;
    i.width = p.latest.width; i.height = p.latest.height;
    i.displayAspect = double(i.width) / double(i.height);
    if (sender.fpsNum && sender.fpsDen) { i.nominalRateNum = int(sender.fpsNum); i.nominalRateDen = int(sender.fpsDen); i.averageFps = double(sender.fpsNum) / sender.fpsDen; }
    else { i.nominalRateNum = 60; i.nominalRateDen = 1; i.averageFps = 60; }
    i.hardwareDecodeActive = false;
    i.videoDecodePath = "frame-input";
    i.color.pixelFormat = pipeline::SourcePixelFormat::Bgra8;
    i.color.range = pipeline::ColorRange::Full;
    i.color.matrix = pipeline::YuvMatrix::BT709;          // as screen capture describes BGRA
    i.color.transfer = pipeline::TransferFunction::SRGB;
    i.color.primaries = pipeline::ColorPrimaries::BT709;
    p.lastFrameTick = GetTickCount64();
    p.say({});
    log::info("frame-input", std::format("opened channel={} sender=\"{}\" pid={} size={}x{} fps={}/{}",
                                         narrow(p.name), sender.name, sender.pid, i.width, i.height,
                                         sender.fpsNum, sender.fpsDen));
    return true;
}

SourceReadStatus FrameInputSource::read(pipeline::FramePacket& packet, const AVFrame** output) {
    auto& p = *p_;
    *output = nullptr;
    if (!p.info.opened) return SourceReadStatus::Error;
    // The first read delivers the frame open() already took.
    const bool have = p.sequence == 0 ? true : p.channel.readNewest(p.latest, 8);
    if (!have) {
        const auto sender = p.channel.sender();
        if (!sender.present || (sender.pid && !sender.alive)) { p.say(L"发送画面的应用已关闭"); return SourceReadStatus::Error; }
        if (GetTickCount64() - p.lastFrameTick > 1000) { p.say(std::format(L"等待「{}」发送画面", widen(sender.name))); p.waiting = true; }
        return SourceReadStatus::Waiting;
    }
    const auto& f = p.latest;
    const int64_t arrival = host100ns();
    int64_t pts = f.timestamp100ns ? f.timestamp100ns + p.clockOffset : arrival;
    if (pts > arrival || pts < arrival - 10000000) pts = arrival;   // a clock that isn't QPC: use the arrival time
    if (pts <= p.lastPts) pts = p.lastPts + 1;
    av_frame_unref(p.frame);
    p.frame->format = f.bgra ? AV_PIX_FMT_BGRA : AV_PIX_FMT_RGBA;
    p.frame->width = int(f.width); p.frame->height = int(f.height);
    if (av_frame_get_buffer(p.frame, 64) < 0) { p.say(L"画面输入内存不足"); return SourceReadStatus::Error; }
    for (uint32_t y = 0; y < f.height; ++y)
        pushCopyOpaqueRow(p.frame->data[0] + size_t(y) * p.frame->linesize[0], f.pixels.data() + size_t(y) * f.width * 4u, f.width);
    p.frame->pts = pts;
    p.frame->color_range = AVCOL_RANGE_JPEG; p.frame->colorspace = AVCOL_SPC_RGB; p.frame->color_trc = AVCOL_TRC_IEC61966_2_1;
    packet = {};
    packet.sequence = ++p.sequence;
    packet.pts = {pts, 10000000};
    packet.duration = p.lastPts ? pipeline::Rational{pts - p.lastPts, 10000000} : pipeline::Rational{p.info.nominalRateDen, p.info.nominalRateNum};
    packet.sourceKind = pipeline::SourceKind::FrameInput;
    packet.colorInfo = p.info.color;
    packet.arrivalHost100ns = arrival;
    if (f.discontinuity || p.waiting) packet.flags |= uint32_t(pipeline::FrameFlagBits::Discontinuity);
    p.waiting = false;
    p.lastPts = pts;
    p.lastFrameTick = GetTickCount64();
    p.say({});
    *output = p.frame;
    return SourceReadStatus::Frame;
}

void FrameInputSource::close() noexcept {
    auto& p = *p_;
    if (p.frame) av_frame_free(&p.frame);
    p.channel.close();
    p.info = {};
    p.sequence = 0; p.lastPts = 0; p.waiting = false;
}

} // namespace veyra::source
