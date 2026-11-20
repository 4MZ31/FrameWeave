// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// A sharper capture format at another frame rate (source/PushCaptureSharper.h). Pure: also builds with g++.
#include "veyra/source/PushCaptureSharper.h"
#include <cstdio>
#include <string>
#include <vector>
using namespace veyra::source;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::printf("FAILED line %d: %s\n", __LINE__, #x); } } while (0)

static CaptureFormat make(int index, unsigned w, unsigned h, double fps, const wchar_t* pixel, int tier, int rank, bool supported = true, bool interlaced = false) {
    CaptureFormat f;
    f.index = index; f.width = w; f.height = h; f.fps = fps; f.pixel = pixel; f.tier = tier; f.rank = rank;
    f.supported = supported; f.interlaced = interlaced; f.interval100ns = captureFrameInterval(fps);
    f.key = std::wstring(L"k") + std::to_wstring(index);
    return f;
}
static std::wstring rateOf(const std::vector<CaptureFormat>& list, unsigned w, unsigned h, double fps) {
    for (const auto& f : list) if (f.width == w && f.height == h && std::abs(f.fps - fps) < 0.5) return captureFrameRateGroupId(f);
    return {};
}

int main() {
    // The card of the report: 1080p as MJPEG at 60 and 30, as YUY2 at 30 and below; 720p as YUY2 at 60.
    const std::vector<CaptureFormat> usb3{
        make(0, 1920, 1080, 60, L"MJPEG", 3, 100), make(1, 1920, 1080, 30, L"MJPEG", 3, 100),
        make(2, 1920, 1080, 30, L"YUY2", 0, 10), make(3, 1920, 1080, 25, L"YUY2", 0, 10), make(4, 1920, 1080, 5, L"YUY2", 0, 10),
        make(5, 1280, 720, 60, L"YUY2", 0, 10), make(6, 1280, 720, 60, L"MJPEG", 3, 100)};
    {   // At 60 the menu holds MJPEG alone: YUY2 at 30 is offered, the report's case.
        const auto* f = pushSharperCaptureFormat(usb3, L"1920x1080", rateOf(usb3, 1920, 1080, 60));
        CHECK(f && f->pixel == L"YUY2" && f->fps == 30 && f->key == L"k2");   // the report: only MJPEG was seen
    }
    {   // Where an uncompressed format already stands in the menu there is nothing to say.
        CHECK(!pushSharperCaptureFormat(usb3, L"1920x1080", rateOf(usb3, 1920, 1080, 30)));
        CHECK(!pushSharperCaptureFormat(usb3, L"1280x720", rateOf(usb3, 1280, 720, 60)));
    }
    {   // Nothing chosen, or a resolution the card does not have.
        CHECK(!pushSharperCaptureFormat(usb3, L"", rateOf(usb3, 1920, 1080, 60)));
        CHECK(!pushSharperCaptureFormat(usb3, L"1920x1080", L""));
        CHECK(!pushSharperCaptureFormat(usb3, L"3840x2160", rateOf(usb3, 1920, 1080, 60)));
    }
    {   // A USB 2 link: YUY2 at 1080p only at 5 a second. That is no use for a game, so nothing is offered.
        const std::vector<CaptureFormat> usb2{make(0, 1920, 1080, 60, L"MJPEG", 3, 100), make(1, 1920, 1080, 30, L"MJPEG", 3, 100), make(2, 1920, 1080, 5, L"YUY2", 0, 10)};
        CHECK(!pushSharperCaptureFormat(usb2, L"1920x1080", rateOf(usb2, 1920, 1080, 60)));   // five frames a second is not offered
    }
    {   // The highest usable rate wins, then the cheaper format; unsupported and interlaced ones are never offered.
        const std::vector<CaptureFormat> mixed{make(0, 1920, 1080, 60, L"MJPEG", 3, 100),
            make(1, 1920, 1080, 30, L"YUY2", 0, 10), make(2, 1920, 1080, 50, L"YUY2", 0, 10), make(3, 1920, 1080, 50, L"NV12", 0, 0),
            make(4, 1920, 1080, 59.94, L"UYVY", 2, 30, false), make(5, 1920, 1080, 50, L"NV12", 0, 0, true, true)};
        const auto* f = pushSharperCaptureFormat(mixed, L"1920x1080", rateOf(mixed, 1920, 1080, 60));
        CHECK(f && f->key == L"k3");
    }
    {   // A format that needs a decoder counts as compressed whatever it is called; 29.97 is 30.
        const std::vector<CaptureFormat> h264{make(0, 1920, 1080, 60, L"H264", 3, 100), make(1, 1920, 1080, 29.97, L"NV12", 0, 0)};
        const auto* f = pushSharperCaptureFormat(h264, L"1920x1080", rateOf(h264, 1920, 1080, 60));
        CHECK(f && f->pixel == L"NV12");
        CHECK(pushCaptureCompressed(h264[0]) && !pushCaptureCompressed(h264[1]));
    }
    {   // A card with compressed formats only: nothing to offer.
        const std::vector<CaptureFormat> only{make(0, 1920, 1080, 60, L"MJPEG", 3, 100), make(1, 1920, 1080, 30, L"MJPEG", 3, 100)};
        CHECK(!pushSharperCaptureFormat(only, L"1920x1080", rateOf(only, 1920, 1080, 60)));
    }
    std::printf(failures ? "%d FAILED\n" : "ALL PASSED (0 failures)\n", failures);
    return failures ? 1 : 0;
}
