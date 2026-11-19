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
#include <string>
#include <thread>
#include <vector>

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

// The width of the picture the layers are drawn on (setPushSourceReferenceWidth).
std::atomic<uint32_t> referenceWidth{0};

// One running source: its thread, the slot it draws into and its newest frame.
struct Worker {
    PushSourceSpec spec;
    std::atomic<int> slot{-1};
    std::atomic<bool> mirror{false}, loop{true};
    std::atomic<int> shape{0};
    std::atomic<float> cropLeft{0}, cropTop{0}, cropRight{0}, cropBottom{0};
    std::atomic<float> share{0};         // the layer's width as a share of the picture's (0: unknown)
    std::atomic<bool> stop{false};
    std::mutex mutex;
    std::shared_ptr<const PushOverlayImage> latest;
    std::wstring error;
    bool running = false;
    uint32_t width = 0, height = 0;
    uint32_t captureWidth = 0, captureHeight = 0;
    std::vector<PushCameraFormat> sizes;   // camera: what it offers
    double fps = 0;
    uint64_t frames = 0;
    Clock::time_point fpsSince = Clock::now();
    uint64_t fpsFrames = 0;
    // For the log, every ten seconds: the rate and what one frame costs this thread.
    Clock::time_point reportSince = Clock::now();
    uint64_t reportFrames = 0;
    double reportWorkMs = 0;
    std::thread thread;
    // The thread holds its own reference, so this runs on it (or after a join): never terminate.
    ~Worker() { stop = true; if (thread.joinable()) { if (thread.get_id() == std::this_thread::get_id()) thread.detach(); else thread.join(); } }

    // The id the interface knows this source by; another scene using the same camera renames it.
    std::string id() { std::lock_guard lock(mutex); return spec.id; }
    void fail(std::wstring why) {
        std::string line;
        {
            std::lock_guard lock(mutex);
            if (error == why) return;
            error = std::move(why);
            running = false;
            line = std::format("{} {}", spec.id, narrow(error));
        }
        veyra::log::warn("push-sources", line);
    }
    // One RGB32 picture from the source (rows `pitch` bytes apart, negative for bottom-up): fitted to
    // the width the layer is shown, cropped, shaped and published (this worker's own thread).
    void publishBgrx(const uint8_t* source, ptrdiff_t pitch, uint32_t w, uint32_t h) {
        const auto started = Clock::now();
        const float l = cropLeft.load(), t = cropTop.load(), r = cropRight.load(), b = cropBottom.load();
        const auto form = PushSourceShape(shape.load());
        const uint32_t shown = pushSourceShownWidth(share.load(), referenceWidth.load());
        PushOverlayImage raw;
        if (l == 0 && t == 0 && r == 0 && b == 0) {
            // Converted and averaged down in one pass: a 4K frame for a corner never exists in full.
            pushBgrxToRgba(source, pitch, w, h, pushSourceFitWidth(w, h, shown, form), raw);
        } else {
            pushBgrxToRgba(source, pitch, w, h, 0, raw);
            cropPushSourceFrame(raw, l, t, r, b);
            fitPushSourceFrame(raw, pushSourceFitWidth(raw.width, raw.height, shown, form));
        }
        if (raw.width < 2 || raw.height < 2) return;
        shapePushSourceFrame(raw, mirror.load(), form);
        auto frame = std::make_shared<const PushOverlayImage>(std::move(raw));
        const int s = slot.load();
        if (s >= 0) setPushLiveLayerFrame(s, frame);
        const auto now = Clock::now();
        std::string report;
        {
            std::lock_guard lock(mutex);
            latest = frame;
            if (!running || !error.empty()) { running = true; error.clear(); }
            width = frame->width; height = frame->height;
            captureWidth = w; captureHeight = h;
            ++frames; ++fpsFrames; ++reportFrames;
            reportWorkMs += std::chrono::duration<double, std::milli>(now - started).count();
            const double seconds = std::chrono::duration<double>(now - fpsSince).count();
            if (seconds >= 1.0) { fps = fpsFrames / seconds; fpsFrames = 0; fpsSince = now; }
            const double reportSeconds = std::chrono::duration<double>(now - reportSince).count();
            if (reportSeconds >= 10.0) {
                report = std::format("{} {}x{} -> {}x{} {:.1f} fps, {:.1f} ms a frame here", spec.id, w, h, width, height,
                                     reportFrames / reportSeconds, reportWorkMs / double(std::max<uint64_t>(1, reportFrames)));
                reportFrames = 0; reportWorkMs = 0; reportSince = now;
            }
        }
        if (!report.empty()) veyra::log::info("push-sources", report);
    }
    bool stopping() const { return stop.load(); }
};

std::mutex workersMutex;
std::map<std::string, std::shared_ptr<Worker>> workers; // by source key (kind + target)

// A camera asked for another resolution is another source: it has to be opened again.
std::string keyOf(const PushSourceSpec& s) {
    return std::to_string(int(s.kind)) + "|" + narrow(s.target) + (s.kind == PushSourceKind::Camera && s.cameraHeight ? "|" + std::to_string(s.cameraHeight) : std::string());
}

struct ComScope {
    bool com = false, mf = false;
    ComScope() {
        com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        mf = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL));
    }
    ~ComScope() { if (mf) MFShutdown(); if (com) CoUninitialize(); }
};

// A subtype's four characters for the log (MJPG, NV12, YUY2, H264); RGB subtypes are numbers.
std::string fourccName(uint32_t fourcc) {
    std::string name;
    for (int i = 0; i < 4; ++i) {
        const char c = char((fourcc >> (8 * i)) & 0xFF);
        if (c < 32 || c > 126) return std::format("#{}", fourcc);
        name.push_back(c);
    }
    return name;
}

// Opens the camera with the format choosePushCameraFormat picks (PushLayerSources.h): automatic is
// the one closest to 1280 x 720 at 30 FPS or more, a chosen height the closest the camera offers.
// What it offers is logged and kept for the interface.
void chooseCameraFormat(IMFSourceReader* reader, Worker& worker) {
    std::vector<ComPtr<IMFMediaType>> types;
    std::vector<PushCameraFormat> formats;
    for (DWORD i = 0;; ++i) {
        ComPtr<IMFMediaType> type;
        if (FAILED(reader->GetNativeMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), i, &type))) break;
        UINT32 w = 0, h = 0, num = 0, den = 0;
        if (FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h)) || !w || !h) continue;
        MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &num, &den);
        GUID subtype{};
        type->GetGUID(MF_MT_SUBTYPE, &subtype);
        formats.push_back({w, h, den ? double(num) / den : 0.0, uint32_t(subtype.Data1)});
        types.push_back(type);
    }
    const auto sizes = pushCameraSizes(formats);
    std::string offered;
    for (const auto& size : sizes) offered += std::format("{}{}x{}@{:.0f}", offered.empty() ? "" : ", ", size.width, size.height, size.fps);
    const int chosen = choosePushCameraFormat(formats, worker.spec.cameraHeight);
    HRESULT hr = S_OK;
    if (chosen >= 0) hr = reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, types[size_t(chosen)].Get());
    const std::string name = worker.id();
    if (chosen >= 0)
        veyra::log::info("push-sources", std::format("{} camera asked {} -> {}x{} {} {:.2f} fps (0x{:08X}); offers {}", name,
            worker.spec.cameraHeight ? std::format("{}p", worker.spec.cameraHeight) : std::string("automatic"),
            formats[size_t(chosen)].width, formats[size_t(chosen)].height, fourccName(formats[size_t(chosen)].fourcc), formats[size_t(chosen)].fps, uint32_t(hr), offered));
    else veyra::log::warn("push-sources", std::format("{} camera lists no formats", name));
    std::lock_guard lock(worker.mutex);
    worker.sizes = sizes;
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

// One RGB32 sample to the worker (IMF2DBuffer gives the real pitch, bottom-up or not).
bool publishSample(IMFSample* sample, uint32_t w, uint32_t h, LONG stride, Worker& worker) {
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return false;
    ComPtr<IMF2DBuffer> buffer2d;
    if (SUCCEEDED(buffer.As(&buffer2d))) {
        BYTE* scan0 = nullptr; LONG pitch = 0;
        if (SUCCEEDED(buffer2d->Lock2D(&scan0, &pitch))) {
            worker.publishBgrx(scan0, ptrdiff_t(pitch), w, h);
            buffer2d->Unlock2D();
            return true;
        }
    }
    BYTE* data = nullptr; DWORD length = 0;
    if (FAILED(buffer->Lock(&data, nullptr, &length))) return false;
    const LONG pitch = stride;
    const uint8_t* first = pitch < 0 ? data + size_t(-pitch) * (h - 1) : data;
    const bool fits = size_t(std::abs(pitch)) * h <= length;
    if (fits) worker.publishBgrx(first, ptrdiff_t(pitch), w, h);
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
        if (SUCCEEDED(hr) && camera) chooseCameraFormat(reader.Get(), worker);
        if (FAILED(hr) || !setRgb32(reader.Get(), w, h, stride)) {
            worker.fail(camera ? std::format(L"The camera could not be opened (0x{:08X}); another app may be using it", uint32_t(hr))
                               : std::format(L"The video could not be opened (0x{:08X})", uint32_t(hr)));
            for (int i = 0; i < 30 && !worker.stopping(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        veyra::log::info("push-sources", std::format("{} {} open {}x{}", worker.id(), camera ? "camera" : "video", w, h));
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
            publishSample(sample.Get(), w, h, stride, worker);
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
            worker.publishBgrx(bits, ptrdiff_t(bw) * 4, uint32_t(bw), uint32_t(bh));
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
            { std::lock_guard frameLock(worker->mutex); worker->spec.id = spec.id; worker->spec.place = spec.place; }
            worker->mirror = spec.mirror;
            worker->loop = spec.loop;
            worker->shape = int(spec.shape);
            worker->cropLeft = spec.cropLeft; worker->cropTop = spec.cropTop;
            worker->cropRight = spec.cropRight; worker->cropBottom = spec.cropBottom;
            worker->share = spec.place.w;
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
        s.captureWidth = worker->captureWidth; s.captureHeight = worker->captureHeight;
        s.sizes = worker->sizes; s.serial = worker->frames;
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

void setPushSourceReferenceWidth(uint32_t width) { referenceWidth.store(width); }

uint64_t pushSourceSerial(const std::string& id) {
    std::lock_guard lock(workersMutex);
    for (const auto& [key, worker] : workers)
        if (worker->spec.id == id) { std::lock_guard frameLock(worker->mutex); return worker->frames; }
    return 0;
}

} // namespace veyra::engine
