// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS Frame Input API: the header's sender (include/pushfps/FrameInput.h) and PushFPS's reader
// (FrameInputChannel) through real named shared memory: frames, newest-wins, size changes, a
// sender writing from another thread while the reader copies (no torn frames), closing, bad names.
#include "pushfps/FrameInput.h"
#include "veyra/source/FrameInputSource.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace veyra::source;
namespace {
int failures = 0;
void check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}
std::vector<uint8_t> solid(uint32_t w, uint32_t h, uint8_t v) {
    std::vector<uint8_t> px(size_t(w) * h * 4);
    for (size_t i = 0; i < px.size(); i += 4) { px[i] = v; px[i + 1] = uint8_t(v + 1); px[i + 2] = uint8_t(v + 2); px[i + 3] = 255; }
    return px;
}
} // namespace

int main() {
    const std::wstring channel = L"pushfps-test-" + std::to_wstring(GetCurrentProcessId());
    check(validFrameInputChannel(L"default") && validFrameInputChannel(L"my_app-2"), "valid channel names");
    check(!validFrameInputChannel(L"") && !validFrameInputChannel(L"a/b") && !validFrameInputChannel(std::wstring(33, L'a')), "invalid channel names");
    std::wstring parsed;
    check(parseFrameInputUri(L"frameinput:", parsed) && parsed == L"default", "an empty channel means default");
    check(parseFrameInputUri(frameInputUri(L"obs"), parsed) && parsed == L"obs", "uri round trip");
    check(!parseFrameInputUri(L"frameinput:..\\x", parsed), "a bad uri is refused");

    FrameInputChannel reader;
    check(!reader.open(channel), "nothing to open before a sender exists");
    {
        pushfps::FrameInputSender sender;
        check(sender.open(channel.c_str(), 64, 32, 60, 1, "Test sender"), "sender opens");
        check(reader.open(channel), "reader opens the channel");
        auto info = reader.sender();
        check(info.present && info.alive && info.name == "Test sender" && info.maxWidth == 64 && info.fpsNum == 60, "sender details");
        FrameInputFrame f;
        check(!reader.readNewest(f, 20), "no frame yet: times out");
        auto a = solid(64, 32, 10);
        check(sender.send(a.data(), 64, 32, 64 * 4, PUSHFPS_FORMAT_BGRA8), "send a frame");
        check(reader.readNewest(f, 200) && f.width == 64 && f.height == 32 && f.bgra && f.pixels[0] == 10 && f.pixels[2] == 12, "read it");
        check(f.timestamp100ns > 0, "sender stamps the time");
        check(!reader.readNewest(f, 20), "the same frame is not read twice");
        // Newest wins: three frames, one read.
        for (uint8_t v : {20, 30, 40}) { auto px = solid(64, 32, v); sender.send(px.data(), 64, 32, 64 * 4, PUSHFPS_FORMAT_RGBA8); }
        check(reader.readNewest(f, 200) && f.pixels[0] == 40 && !f.bgra, "only the newest frame is read");
        // A smaller frame with padding in the rows.
        std::vector<uint8_t> padded(size_t(40) * 16 * 4 + 16 * 8, 0);
        for (uint32_t y = 0; y < 16; ++y) for (uint32_t x = 0; x < 40; ++x) padded[y * (40 * 4 + 8) + x * 4] = uint8_t(x);
        check(sender.send(padded.data(), 40, 16, 40 * 4 + 8, PUSHFPS_FORMAT_BGRA8, 0, true), "send a smaller padded frame");
        check(reader.readNewest(f, 200) && f.width == 40 && f.height == 16 && f.pixels[39 * 4] == 39 && f.pixels[40 * 4] == 0 && f.discontinuity,
              "size change, rows unpadded, discontinuity kept");
        check(!sender.send(a.data(), 65, 32, 65 * 4, PUSHFPS_FORMAT_BGRA8), "a frame above the maximum is refused");
        // Torn-read test: a thread sends frames whose every byte is the same value; each read must be uniform.
        std::atomic<bool> stop{false};
        std::thread writer([&] {
            uint8_t v = 0;
            while (!stop) { std::vector<uint8_t> px(size_t(64) * 32 * 4, ++v); sender.send(px.data(), 64, 32, 64 * 4, PUSHFPS_FORMAT_BGRA8); }
        });
        int reads = 0, torn = 0;
        for (int i = 0; i < 400; ++i) {
            if (!reader.readNewest(f, 50)) continue;
            ++reads;
            for (uint8_t b : f.pixels) if (b != f.pixels[0]) { ++torn; break; }
        }
        stop = true; writer.join();
        check(reads > 50, "many frames read while writing (" + std::to_string(reads) + ")");
        check(torn == 0, "no torn frames (" + std::to_string(torn) + ")");
        auto found = FrameInputChannel::discover({channel});
        bool listed = false;
        for (const auto& [name, s] : found) if (name == channel && s.name == "Test sender") listed = true;
        check(listed, "discover lists the channel");
    }   // the sender closes
    FrameInputFrame f;
    check(!reader.readNewest(f, 20), "a closed channel reads nothing");
    check(!reader.sender().present, "a closed channel is reported");
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
