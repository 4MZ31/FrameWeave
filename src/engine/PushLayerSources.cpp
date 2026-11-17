// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS streaming mode sources: webcam, window and video file threads (engine/PushLayerSources.h).
#include "veyra/engine/PushLayerSources.h"
#include "veyra/Log.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <format>
#include <map>
#include <mutex>
#include <thread>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace veyra::engine {
namespace {
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(std::max(0, n)), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

// One running source: its thread, the slot it draws into and its newest frame.
struct Worker {
    PushSourceSpec spec;
    std::atomic<int> slot{-1};
    std::atomic<bool> mirror{false}, loop{true};
    std::atomic<int> shape{0};
    std::atomic<bool> stop{false};
    std::mutex mutex;
    std::shared_ptr<const PushOverlayImage> latest;
    std::wstring error;
    bool running = false;
    uint32_t width = 0, height = 0;
    double fps = 0;
    uint64_t frames = 0;
    Clock::time_point fpsSince = Clock::now();
    uint64_t fpsFrames = 0;
    std::thread thread;
    // The thread holds its own reference, so this runs on it (or after a join): never terminate.
    ~Worker() { stop = true; if (thread.joinable()) { if (thread.get_id() == std::this_thread::get_id()) thread.detach(); else thread.join(); } }

    void fail(std::wstring why) {
        {
            std::lock_guard lock(mutex);
            if (error == why) return;
            error = std::move(why);
            running = false;
        }
        veyra::log::warn("push-sources", std::format("{} {}", spec.id, narrow(error)));
    }
    // Shapes and publishes one frame (any thread of this worker).
    void publish(PushOverlayImage&& raw) {
        shapePushSourceFrame(raw, mirror.load(), PushSourceShape(shape.load()));
        auto frame = std::make_shared<const PushOverlayImage>(std::move(raw));
        const int s = slot.load();
        if (s >= 0) setPushLiveLayerFrame(s, frame);
        std::lock_guard lock(mutex);
        latest = frame;
        if (!running || !error.empty()) { running = true; error.clear(); }
        width = frame->width; height = frame->height;
        ++frames; ++fpsFrames;
        const double seconds = std::chrono::duration<double>(Clock::now() - fpsSince).count();
        if (seconds >= 1.0) { fps = fpsFrames / seconds; fpsFrames = 0; fpsSince = Clock::now(); }
    }
    bool stopping() const { return stop.load(); }
};

std::mutex workersMutex;
std::map<std::string, std::shared_ptr<Worker>> workers; // by source key (kind + target)

std::string keyOf(const PushSourceSpec& s) { return std::to_string(int(s.kind)) + "|" + narrow(s.target); }

struct ComScope {
    bool com = false, mf = false;
    ComScope() {
        com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        mf = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL));
    }
    ~ComScope() { if (mf) MFShutdown(); if (com) CoUninitialize(); }
};

// RGB32 (B, G, R, unused) rows into straight-alpha RGBA.
void rgb32ToRgba(const uint8_t* src, LONG pitch, uint32_t w, uint32_t h, PushOverlayImage& out) {
    out.width = w; out.height = h;
    out.rgba.resize(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* row = src + LONG(y) * pitch;
        uint8_t* dst = &out.rgba[size_t(y) * w * 4];
        for (uint32_t x = 0; x < w; ++x) {
            dst[x * 4 + 0] = row[x * 4 + 2];
            dst[x * 4 + 1] = row[x * 4 + 1];
            dst[x * 4 + 2] = row[x * 4 + 0];
            dst[x * 4 + 3] = 255;
        }
    }
}

// Picks the camera format closest to 1280 x 720 at 30 FPS or more (never above 1920 wide).
void chooseCameraFormat(IMFSourceReader* reader) {
    ComPtr<IMFMediaType> best;
    double bestScore = 1e18;
    for (DWORD i = 0;; ++i) {
        ComPtr<IMFMediaType> type;
        if (FAILED(reader->GetNativeMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), i, &type))) break;
        UINT32 w = 0, h = 0, num = 0, den = 0;
        if (FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h)) || !w || !h) continue;
        MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &num, &den);
        const double fps = den ? double(num) / den : 0;
        double score = std::abs(double(w) - 1280) + std::abs(double(h) - 720);
        if (w > 1920) score += 1e6;
        if (fps < 29) score += 1e5 + (30 - fps) * 100;
        if (score < bestScore) { bestScore = score; best = type; }
    }
    if (best) reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, best.Get());
}

bool setRgb32(IMFSourceReader* reader, uint32_t& w, uint32_t& h, LONG& stride) {
    ComPtr<IMFMediaType> rgb;
    if (FAILED(MFCreateMediaType(&rgb))) return false;
    rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, rgb.Get()))) return false;
    ComPtr<IMFMediaType> current;
    if (FAILED(reader->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &current))) return false;
    UINT32 cw = 0, ch = 0;
    if (FAILED(MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &cw, &ch)) || cw < 2 || ch < 2) return false;
    w = cw; h = ch;
    UINT32 s = 0;
    if (SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &s))) stride = LONG(int32_t(s));
    else { LONG computed = 0; stride = SUCCEEDED(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, w, &computed)) ? computed : LONG(w * 4); }
    return true;
}

// One RGB32 sample into a frame (IMF2DBuffer gives the real pitch, bottom-up or not).
bool sampleToFrame(IMFSample* sample, uint32_t w, uint32_t h, LONG stride, PushOverlayImage& out) {
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return false;
    ComPtr<IMF2DBuffer> buffer2d;
    if (SUCCEEDED(buffer.As(&buffer2d))) {
        BYTE* scan0 = nullptr; LONG pitch = 0;
        if (SUCCEEDED(buffer2d->Lock2D(&scan0, &pitch))) {
            rgb32ToRgba(scan0, pitch, w, h, out);
            buffer2d->Unlock2D();
            return true;
        }
    }
    BYTE* data = nullptr; DWORD length = 0;
    if (FAILED(buffer->Lock(&data, nullptr, &length))) return false;
    const LONG pitch = stride;
    const uint8_t* first = pitch < 0 ? data + size_t(-pitch) * (h - 1) : data;
    const bool fits = size_t(std::abs(pitch)) * h <= length;
    if (fits) rgb32ToRgba(first, pitch, w, h, out);
    buffer->Unlock();
    return fits;
}

void runReader(Worker& worker) {
    ComScope scope;
    if (!scope.mf) { worker.fail(L"Media Foundation unavailable"); return; }
    const bool camera = worker.spec.kind == PushSourceKind::Camera;
    while (!worker.stopping()) {
        ComPtr<IMFAttributes> readerAttributes;
        MFCreateAttributes(&readerAttributes, 2);
        readerAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        ComPtr<IMFSourceReader> reader;
        ComPtr<IMFMediaSource> source;
        HRESULT hr = E_FAIL;
        if (camera) {
            ComPtr<IMFAttributes> attributes;
            MFCreateAttributes(&attributes, 2);
            attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
            attributes->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, worker.spec.target.c_str());
            hr = MFCreateDeviceSource(attributes.Get(), &source);
            if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(source.Get(), readerAttributes.Get(), &reader);
        } else {
            hr = MFCreateSourceReaderFromURL(worker.spec.target.c_str(), readerAttributes.Get(), &reader);
            if (SUCCEEDED(hr)) {
                reader->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS), FALSE);
                reader->SetStreamSelection(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
            }
        }
        uint32_t w = 0, h = 0;
        LONG stride = 0;
        if (SUCCEEDED(hr) && camera) chooseCameraFormat(reader.Get());
        if (FAILED(hr) || !setRgb32(reader.Get(), w, h, stride)) {
            worker.fail(camera ? std::format(L"The camera could not be opened (0x{:08X}); another app may be using it", uint32_t(hr))
                               : std::format(L"The video could not be opened (0x{:08X})", uint32_t(hr)));
            for (int i = 0; i < 30 && !worker.stopping(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        veyra::log::info("push-sources", std::format("{} {} open {}x{}", worker.spec.id, camera ? "camera" : "video", w, h));
        Clock::time_point base{};
        LONGLONG baseTs = -1;
        bool reopen = false;
        while (!worker.stopping() && !reopen) {
            DWORD index = 0, flags = 0;
            LONGLONG ts = 0;
            ComPtr<IMFSample> sample;
            hr = reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &index, &flags, &ts, &sample);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
                worker.fail(camera ? L"The camera stopped (unplugged, or taken by another app)" : L"The video could not be read");
                reopen = true;
                break;
            }
            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
                if (!setRgb32(reader.Get(), w, h, stride)) { reopen = true; break; }
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                if (camera || !worker.loop.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
                PROPVARIANT start;
                PropVariantInit(&start);
                start.vt = VT_I8;
                start.hVal.QuadPart = 0;
                reader->SetCurrentPosition(GUID_NULL, start);
                baseTs = -1;
                continue;
            }
            if (!sample) continue;
            // A video file plays at its own speed.
            if (!camera) {
                const auto now = Clock::now();
                if (baseTs < 0 || ts < baseTs) { baseTs = ts; base = now; }
                const auto due = base + std::chrono::microseconds((ts - baseTs) / 10);
                if (due > now + std::chrono::seconds(2)) { baseTs = ts; base = now; }
                else if (due > now) std::this_thread::sleep_until(due);
            }
            PushOverlayImage frame;
            if (sampleToFrame(sample.Get(), w, h, stride, frame)) worker.publish(std::move(frame));
        }
        if (source) source->Shutdown();
        if (reopen) for (int i = 0; i < 20 && !worker.stopping(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// A window drawn by Windows itself (PrintWindow with PW_RENDERFULLCONTENT also draws DirectComposition
// content such as browsers), about 15 times a second, at most 1920 wide.
void runWindow(Worker& worker) {
    const HWND window = reinterpret_cast<HWND>(uintptr_t(std::wcstoull(worker.spec.target.c_str(), nullptr, 10)));
    HDC screen = GetDC(nullptr);
    HDC full = CreateCompatibleDC(screen), reduced = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    HBITMAP fullBitmap = nullptr, reducedBitmap = nullptr;
    void* fullBits = nullptr; void* reducedBits = nullptr;
    int fullW = 0, fullH = 0, outW = 0, outH = 0;
    auto make = [&](HDC dc, HBITMAP& bitmap, void*& bits, int w, int h) {
        if (bitmap) { DeleteObject(bitmap); bitmap = nullptr; }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h; // top-down
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bitmap) SelectObject(dc, bitmap);
        return bitmap != nullptr;
    };
    while (!worker.stopping()) {
        const auto next = Clock::now() + std::chrono::milliseconds(66);
        if (!IsWindow(window)) { worker.fail(L"The window was closed"); std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }
        if (IsIconic(window)) { worker.fail(L"The window is minimised"); std::this_thread::sleep_for(std::chrono::milliseconds(250)); continue; }
        RECT client{};
        GetClientRect(window, &client);
        const int w = client.right - client.left, h = client.bottom - client.top;
        if (w < 2 || h < 2) { worker.fail(L"The window has no picture"); std::this_thread::sleep_for(std::chrono::milliseconds(250)); continue; }
        if (w != fullW || h != fullH) {
            if (!make(full, fullBitmap, fullBits, w, h)) { worker.fail(L"Out of memory for the window picture"); break; }
            fullW = w; fullH = h;
            const double scale = std::min(1.0, 1920.0 / w);
            outW = std::max(2, int(w * scale)) & ~1; outH = std::max(2, int(h * scale)) & ~1;
            if (scale < 1.0 && !make(reduced, reducedBitmap, reducedBits, outW, outH)) { worker.fail(L"Out of memory for the window picture"); break; }
        }
        if (!PrintWindow(window, full, PW_CLIENTONLY | PW_RENDERFULLCONTENT)) {
            worker.fail(L"Windows did not draw the window");
        } else {
            const uint8_t* bits = static_cast<const uint8_t*>(fullBits);
            int bw = fullW, bh = fullH;
            if (outW != fullW || outH != fullH) {
                SetStretchBltMode(reduced, HALFTONE);
                SetBrushOrgEx(reduced, 0, 0, nullptr);
                StretchBlt(reduced, 0, 0, outW, outH, full, 0, 0, fullW, fullH, SRCCOPY);
                bits = static_cast<const uint8_t*>(reducedBits); bw = outW; bh = outH;
            }
            GdiFlush();
            PushOverlayImage frame;
            rgb32ToRgba(bits, LONG(bw) * 4, uint32_t(bw), uint32_t(bh), frame);
            worker.publish(std::move(frame));
        }
        std::this_thread::sleep_until(next);
    }
    if (fullBitmap) DeleteObject(fullBitmap);
    if (reducedBitmap) DeleteObject(reducedBitmap);
    DeleteDC(full); DeleteDC(reduced);
}

void startWorker(const std::shared_ptr<Worker>& worker) {
    worker->thread = std::thread([w = worker]() {
        SetThreadDescription(GetCurrentThread(), L"PushFPS scene source");
        if (w->spec.kind == PushSourceKind::Window) runWindow(*w);
        else runReader(*w);
    });
}

void stopWorker(const std::shared_ptr<Worker>& worker) {
    worker->stop = true;
    // A camera read can block up to a frame; never wait on the interface thread for it.
    std::thread([w = worker]() { if (w->thread.joinable()) w->thread.join(); }).detach();
}

} // namespace

std::vector<PushCameraInfo> pushCameras() {
    std::vector<PushCameraInfo> out;
    ComScope scope;
    ComPtr<IMFAttributes> attributes;
    if (FAILED(MFCreateAttributes(&attributes, 1))) return out;
    attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    if (FAILED(MFEnumDeviceSources(attributes.Get(), &devices, &count))) return out;
    for (UINT32 i = 0; i < count; ++i) {
        WCHAR* name = nullptr; WCHAR* link = nullptr; UINT32 length = 0;
        if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &length)) && link) {
            devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length);
            out.push_back({link, name ? name : L"Camera"});
        }
        if (name) CoTaskMemFree(name);
        if (link) CoTaskMemFree(link);
        devices[i]->Release();
    }
    CoTaskMemFree(devices);
    return out;
}

void setPushSources(const std::vector<PushSourceSpec>& sources, uint32_t fadeMs) {
    std::vector<std::shared_ptr<Worker>> stopped;
    std::array<bool, kPushLiveLayers> used{};
    {
        std::lock_guard lock(workersMutex);
        std::map<std::string, std::shared_ptr<Worker>> next;
        int slot = 0;
        for (const auto& spec : sources) {
            if (slot >= kPushLiveLayers) break;
            if (spec.target.empty()) continue;
            const std::string key = keyOf(spec);
            if (next.count(key)) continue;
            std::shared_ptr<Worker> worker;
            if (auto it = workers.find(key); it != workers.end()) { worker = it->second; workers.erase(it); }
            const bool fresh = !worker;
            if (fresh) { worker = std::make_shared<Worker>(); worker->spec = spec; }
            worker->spec.id = spec.id;
            worker->spec.place = spec.place;
            worker->mirror = spec.mirror;
            worker->loop = spec.loop;
            worker->shape = int(spec.shape);
            const int previous = worker->slot.exchange(slot);
            // A source moving to another slot takes its newest frame along.
            if (!fresh && previous != slot) {
                std::lock_guard frameLock(worker->mutex);
                setPushLiveLayerFrame(slot, worker->latest);
            } else if (fresh) setPushLiveLayerFrame(slot, nullptr);
            PushLayerPlace place = spec.place;
            place.visible = true;
            setPushLiveLayerPlace(slot, place, fadeMs);
            used[size_t(slot)] = true;
            if (fresh) startWorker(worker);
            next[key] = worker;
            ++slot;
        }
        for (auto& [key, worker] : workers) stopped.push_back(worker);
        workers = std::move(next);
    }
    // Slots no scene source uses fade out; their last frame stays until it is gone.
    for (int s = 0; s < kPushLiveLayers; ++s)
        if (!used[size_t(s)]) setPushLiveLayerPlace(s, PushLayerPlace{}, fadeMs);
    for (auto& worker : stopped) { worker->slot = -1; stopWorker(worker); }
    veyra::log::info("push-sources", std::format("scene sources {} (stopped {})", sources.size(), stopped.size()));
}

std::vector<PushSourceStatus> pushSourceStatus() {
    std::vector<PushSourceStatus> out;
    std::lock_guard lock(workersMutex);
    for (const auto& [key, worker] : workers) {
        std::lock_guard frameLock(worker->mutex);
        PushSourceStatus s;
        s.id = worker->spec.id; s.running = worker->running && worker->error.empty(); s.error = worker->error;
        s.width = worker->width; s.height = worker->height; s.fps = worker->fps;
        if (Clock::now() - worker->fpsSince > std::chrono::seconds(2)) s.fps = 0;
        out.push_back(std::move(s));
    }
    return out;
}

std::shared_ptr<const PushOverlayImage> pushSourceFrame(const std::string& id) {
    std::lock_guard lock(workersMutex);
    for (const auto& [key, worker] : workers)
        if (worker->spec.id == id) { std::lock_guard frameLock(worker->mutex); return worker->latest; }
    return nullptr;
}

} // namespace veyra::engine
