// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS Frame Input API: the shared-memory reader (no FFmpeg; tests/unit/FrameInputTests.cpp).
#include "veyra/source/FrameInputSource.h"
#include "pushfps/FrameInput.h"

#include <windows.h>
#include <algorithm>
#include <cstring>

namespace veyra::source {

bool validFrameInputChannel(const std::wstring& channel) {
    if (channel.empty() || channel.size() > 32) return false;
    return std::all_of(channel.begin(), channel.end(), [](wchar_t c) {
        return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'-' || c == L'_';
    });
}
std::wstring frameInputUri(const std::wstring& channel) { return L"frameinput:" + (channel.empty() ? std::wstring(L"default") : channel); }
bool parseFrameInputUri(const std::wstring& uri, std::wstring& channel) {
    if (!uri.starts_with(L"frameinput:")) return false;
    channel = uri.substr(11);
    if (channel.empty()) channel = L"default";
    return validFrameInputChannel(channel);
}

struct FrameInputChannel::Impl {
    HANDLE mapping = nullptr, ready = nullptr;
    const uint8_t* view = nullptr;
    size_t viewBytes = 0;
    uint64_t lastCounter = 0;
    const pushfps_frame_input_header* header() const { return reinterpret_cast<const pushfps_frame_input_header*>(view); }
    void close() {
        if (view) { UnmapViewOfFile(view); view = nullptr; }
        if (ready) { CloseHandle(ready); ready = nullptr; }
        if (mapping) { CloseHandle(mapping); mapping = nullptr; }
        viewBytes = 0; lastCounter = 0;
    }
};

FrameInputChannel::FrameInputChannel() : p_(std::make_unique<Impl>()) {}
FrameInputChannel::~FrameInputChannel() { p_->close(); }
void FrameInputChannel::close() { p_->close(); }
bool FrameInputChannel::isOpen() const { return p_->view != nullptr; }

bool FrameInputChannel::open(const std::wstring& channel) {
    p_->close();
    if (!validFrameInputChannel(channel)) return false;
    const std::wstring base = L"Local\\PushFPS.FrameInput." + channel;
    p_->mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, base.c_str());
    if (!p_->mapping) return false;
    // The header first, then the whole mapping once its size is known and checked.
    auto* head = static_cast<const uint8_t*>(MapViewOfFile(p_->mapping, FILE_MAP_READ, 0, 0, PUSHFPS_FRAME_INPUT_HEADER_BYTES));
    if (!head) { p_->close(); return false; }
    pushfps_frame_input_header h{};
    std::memcpy(&h, head, sizeof h);
    UnmapViewOfFile(head);
    if (h.magic != PUSHFPS_FRAME_INPUT_MAGIC || h.version != PUSHFPS_FRAME_INPUT_VERSION || h.headerBytes != PUSHFPS_FRAME_INPUT_HEADER_BYTES ||
        h.slotCount != PUSHFPS_FRAME_INPUT_SLOTS || !h.maxWidth || !h.maxHeight || h.maxWidth > PUSHFPS_FRAME_INPUT_MAX_SIDE ||
        h.maxHeight > PUSHFPS_FRAME_INPUT_MAX_SIDE || h.slotBytes < uint64_t(h.maxWidth) * h.maxHeight * 4u ||
        h.slotBytes > uint64_t(PUSHFPS_FRAME_INPUT_MAX_SIDE) * PUSHFPS_FRAME_INPUT_MAX_SIDE * 4u) { p_->close(); return false; }
    const uint64_t bytes = PUSHFPS_FRAME_INPUT_HEADER_BYTES + h.slotBytes * PUSHFPS_FRAME_INPUT_SLOTS;
    p_->view = static_cast<const uint8_t*>(MapViewOfFile(p_->mapping, FILE_MAP_READ, 0, 0, size_t(bytes)));
    if (!p_->view) { p_->close(); return false; }
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p_->view, &mbi, sizeof mbi) || mbi.RegionSize < bytes) { p_->close(); return false; }   // smaller than it claims
    p_->viewBytes = size_t(bytes);
    p_->ready = OpenEventW(SYNCHRONIZE, FALSE, (base + L".Ready").c_str());   // optional
    return true;
}

FrameInputSender FrameInputChannel::sender() const {
    FrameInputSender s;
    if (!p_->view) return s;
    const auto* h = p_->header();
    s.present = h->magic == PUSHFPS_FRAME_INPUT_MAGIC;
    s.pid = h->senderPid; s.maxWidth = h->maxWidth; s.maxHeight = h->maxHeight;
    s.fpsNum = h->fpsNum; s.fpsDen = h->fpsDen; s.frames = h->frameCounter;
    char name[65]{};
    std::memcpy(name, h->senderName, 64);
    s.name = name;
    if (HANDLE process = s.pid ? OpenProcess(SYNCHRONIZE, FALSE, s.pid) : nullptr) {
        s.alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
        CloseHandle(process);
    }
    return s;
}

bool FrameInputChannel::readNewest(FrameInputFrame& out, uint32_t timeoutMs) {
    if (!p_->view) return false;
    const auto* h = p_->header();
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {
        if (h->magic != PUSHFPS_FRAME_INPUT_MAGIC) return false;   // the sender closed the channel
        const uint64_t counter = h->frameCounter;
        if (counter != p_->lastCounter && counter != 0) {
            for (int attempt = 0; attempt < 4; ++attempt) {
                MemoryBarrier();
                const uint32_t index = h->latestSlot % PUSHFPS_FRAME_INPUT_SLOTS;
                const auto& slot = h->slots[index];
                const uint32_t before = slot.seq;
                MemoryBarrier();
                if (before & 1u) { YieldProcessor(); continue; }
                const uint32_t w = slot.width, ht = slot.height, stride = slot.stride, format = slot.format;
                if (!w || !ht || w > h->maxWidth || ht > h->maxHeight || stride < w * 4u || uint64_t(stride) * ht > h->slotBytes ||
                    (format != PUSHFPS_FORMAT_BGRA8 && format != PUSHFPS_FORMAT_RGBA8)) break;
                out.width = w; out.height = ht; out.bgra = format == PUSHFPS_FORMAT_BGRA8;
                out.discontinuity = (slot.flags & 1u) != 0; out.timestamp100ns = slot.timestamp100ns;
                out.pixels.resize(size_t(w) * ht * 4u);
                const uint8_t* src = p_->view + PUSHFPS_FRAME_INPUT_HEADER_BYTES + uint64_t(index) * h->slotBytes;
                for (uint32_t y = 0; y < ht; ++y) std::memcpy(out.pixels.data() + size_t(y) * w * 4u, src + size_t(y) * stride, size_t(w) * 4u);
                MemoryBarrier();
                if (slot.seq != before) continue;   // rewritten while copying: read again
                p_->lastCounter = counter;
                out.counter = counter;
                return true;
            }
        }
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) return false;
        const DWORD wait = DWORD(std::min<ULONGLONG>(deadline - now, 5));
        if (p_->ready) WaitForSingleObject(p_->ready, wait); else Sleep(1);
    }
}

std::vector<std::pair<std::wstring, FrameInputSender>> FrameInputChannel::discover(const std::vector<std::wstring>& extra) {
    std::vector<std::wstring> names{L"default", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8"};
    for (const auto& e : extra) if (validFrameInputChannel(e) && std::find(names.begin(), names.end(), e) == names.end()) names.push_back(e);
    std::vector<std::pair<std::wstring, FrameInputSender>> out;
    for (const auto& n : names) {
        FrameInputChannel c;
        if (!c.open(n)) continue;
        const auto s = c.sender();
        if (s.present) out.emplace_back(n, s);
    }
    return out;
}

} // namespace veyra::source
