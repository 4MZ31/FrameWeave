// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS Frame Input API (include/pushfps/FrameInput.h, docs/FRAME-INPUT-API.md): frames another
// application writes into a named shared-memory channel, read as a live source ("frameinput:<channel>"),
// like a capture card. FrameInputChannel is the reader (no FFmpeg, unit-tested); FrameInputSource
// turns its frames into BGRA AVFrames for the graph's ingress.
#pragma once
#include "veyra/source/IFrameSource.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace veyra::source {

bool validFrameInputChannel(const std::wstring& channel);
// "frameinput:<channel>" (default channel "default").
std::wstring frameInputUri(const std::wstring& channel);
bool parseFrameInputUri(const std::wstring& uri, std::wstring& channel);

struct FrameInputFrame {
    uint32_t width = 0, height = 0;
    bool bgra = true;                 // false: RGBA
    bool discontinuity = false;
    int64_t timestamp100ns = 0;       // the sender's QPC time in 100 ns, 0 = unknown
    uint64_t counter = 0;             // the channel's frameCounter when read
    std::vector<uint8_t> pixels;      // width * 4 bytes per row, top row first
};
struct FrameInputSender {
    bool present = false;             // a valid channel is open
    bool alive = false;               // its sending process still runs
    std::string name;
    uint32_t pid = 0, maxWidth = 0, maxHeight = 0, fpsNum = 0, fpsDen = 0;
    uint64_t frames = 0;
};

class FrameInputChannel {
public:
    FrameInputChannel();
    ~FrameInputChannel();
    FrameInputChannel(const FrameInputChannel&) = delete;
    FrameInputChannel& operator=(const FrameInputChannel&) = delete;
    // Opens an existing channel (the sender creates it). False when nothing sends on it.
    bool open(const std::wstring& channel);
    void close();
    bool isOpen() const;
    FrameInputSender sender() const;
    // Waits up to timeoutMs for a frame newer than the last one read and copies it. False on timeout,
    // a torn read that kept failing, or a closed channel.
    bool readNewest(FrameInputFrame& out, uint32_t timeoutMs);
    // Senders found on the usual channels ("default", "1".."8") and any extra names given.
    static std::vector<std::pair<std::wstring, FrameInputSender>> discover(const std::vector<std::wstring>& extra = {});
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

class FrameInputSource final : public IFrameSource {
public:
    FrameInputSource();
    ~FrameInputSource() override;
    bool open(const SourceOpenDesc& desc) override;   // desc.path = frameinput:<channel>
    const SourceInfo& info() const override;
    SourceReadStatus read(pipeline::FramePacket& out, const AVFrame** decodedFrame) override;
    bool seek(const pipeline::Rational&) override { return false; }
    void close() noexcept override;
    std::wstring status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace veyra::source
