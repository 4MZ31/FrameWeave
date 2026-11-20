// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Reading the text on a picture with Windows' own recogniser (ui/PushOcr.h). No Qt in this file.
// The picture is decoded with the Windows Imaging Component, so that an HDR frame (linear half
// floats) can be brought down by PushFPS's own rule instead of being cut off at white; the
// recognition is Windows.Media.Ocr. Everything Windows can throw or refuse is caught here: a caller
// gets a result, never an exception.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "veyra/ui/PushOcr.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

#include <climits>

#pragma comment(lib, "windowscodecs.lib")

namespace veyra::ui::ocr {
namespace {
using Microsoft::WRL::ComPtr;
// Named one by one: this namespace has a Language of its own.
using winrt::Windows::Foundation::Rect;
using winrt::Windows::Graphics::Imaging::BitmapAlphaMode;
using winrt::Windows::Graphics::Imaging::BitmapPixelFormat;
using winrt::Windows::Graphics::Imaging::SoftwareBitmap;
using winrt::Windows::Media::Ocr::OcrEngine;
using winrt::Windows::Storage::Streams::DataWriter;

// The calling thread joins the multi-threaded apartment for the length of one call. A thread that is
// already in an apartment of another kind keeps it (the call then fails cleanly inside the try).
struct Apartment {
    bool entered = false;
    Apartment() noexcept {
        try { winrt::init_apartment(winrt::apartment_type::multi_threaded); entered = true; } catch (...) {}
    }
    ~Apartment() { if (entered) winrt::uninit_apartment(); }
};

// 0x80070002 and 0x80070003: the file, or its folder, is not there.
bool notThere(HRESULT code) {
    return code == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || code == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
}

// A pixel format that holds floating point values: an HDR picture.
bool floating(const WICPixelFormatGUID& format) {
    for (const GUID* known : {&GUID_WICPixelFormat64bppRGBAHalf, &GUID_WICPixelFormat64bppRGBHalf, &GUID_WICPixelFormat48bppRGBHalf,
                              &GUID_WICPixelFormat128bppRGBAFloat, &GUID_WICPixelFormat128bppPRGBAFloat, &GUID_WICPixelFormat128bppRGBFloat,
                              &GUID_WICPixelFormat32bppGrayFloat, &GUID_WICPixelFormat16bppGrayHalf})
        if (IsEqualGUID(format, *known)) return true;
    return false;
}
}

uint32_t maxDimension() {
    try { return OcrEngine::MaxImageDimension(); } catch (...) { return 0; }
}

bool loadPicture(const std::wstring& path, Region region, uint32_t largest, Picture& picture, std::wstring& error) {
    picture = {};
    error = L"failed";
    try {
        Apartment apartment;
        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return false;
        ComPtr<IWICBitmapDecoder> decoder;
        const HRESULT opened = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(opened)) { if (notThere(opened)) error = L"no-file"; return false; }
        ComPtr<IWICBitmapFrameDecode> frame;
        UINT fileWidth = 0, fileHeight = 0;
        WICPixelFormatGUID format{};
        if (FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&fileWidth, &fileHeight)) || FAILED(frame->GetPixelFormat(&format))) return false;
        const Plan plan = planFor(fileWidth, fileHeight, region, largest);
        if (!plan.valid()) return false;
        const WICRect cut{INT(plan.sourceX), INT(plan.sourceY), INT(plan.sourceWidth), INT(plan.sourceHeight)};
        const size_t pixels = size_t(plan.sourceWidth) * size_t(plan.sourceHeight);
        if (pixels > size_t(UINT_MAX) / 8) return false;
        std::vector<uint8_t> bgra(pixels * 4);
        bool hdr = false;
        if (floating(format)) {
            // Linear half floats, as PushFPS saves an HDR frame. Another floating format is converted to them first.
            ComPtr<IWICBitmapSource> source;
            if (IsEqualGUID(format, GUID_WICPixelFormat64bppRGBAHalf)) source = frame;
            else if (FAILED(WICConvertBitmapSource(GUID_WICPixelFormat64bppRGBAHalf, frame.Get(), &source))) source.Reset();
            if (source) {
                std::vector<uint16_t> half(pixels * 4);
                if (FAILED(source->CopyPixels(&cut, plan.sourceWidth * 8, UINT(pixels * 8), reinterpret_cast<BYTE*>(half.data())))) return false;
                // The largest and the smallest pattern among the colours: for numbers that are not negative,
                // the order of the patterns is the order of the values. A negative one counts as 0.
                uint16_t brightest = 0, darkest = 0x7BFFu;
                for (size_t i = 0; i < pixels; ++i)
                    for (size_t c = 0; c < 3; ++c) {
                        const uint16_t bits = half[i * 4 + c];
                        if (bits >= 0x8000u) { darkest = 0; continue; }
                        if (bits >= 0x7C00u) continue;   // infinite, or not a number
                        if (bits > brightest) brightest = bits;
                        if (bits < darkest) darkest = bits;
                    }
                const float peak = hdrPeak(brightest), low = hdrFloor(darkest, peak);
                std::vector<uint8_t> table(65536);
                for (uint32_t bits = 0; bits < 65536; ++bits) table[bits] = hdrByte(halfToFloat(uint16_t(bits)), low, peak);
                for (size_t i = 0; i < pixels; ++i) {
                    bgra[i * 4 + 0] = table[half[i * 4 + 2]];
                    bgra[i * 4 + 1] = table[half[i * 4 + 1]];
                    bgra[i * 4 + 2] = table[half[i * 4 + 0]];
                    bgra[i * 4 + 3] = 255;
                }
                hdr = true;
            }
        }
        if (!hdr) {
            ComPtr<IWICBitmapSource> source;
            if (IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) source = frame;
            else if (FAILED(WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, frame.Get(), &source)) || !source) return false;
            if (FAILED(source->CopyPixels(&cut, plan.sourceWidth * 4, UINT(pixels * 4), bgra.data()))) return false;
            for (size_t i = 0; i < pixels; ++i) bgra[i * 4 + 3] = 255;
        }
        if (plan.scaled()) {
            const size_t scaledBytes = size_t(plan.width) * size_t(plan.height) * 4;
            if (scaledBytes > size_t(UINT_MAX)) return false;
            ComPtr<IWICBitmap> bitmap;
            ComPtr<IWICBitmapScaler> scaler;
            if (FAILED(factory->CreateBitmapFromMemory(plan.sourceWidth, plan.sourceHeight, GUID_WICPixelFormat32bppBGRA, plan.sourceWidth * 4,
                                                       UINT(bgra.size()), bgra.data(), &bitmap))
                || FAILED(factory->CreateBitmapScaler(&scaler))
                || FAILED(scaler->Initialize(bitmap.Get(), plan.width, plan.height,
                                             plan.width > plan.sourceWidth ? WICBitmapInterpolationModeCubic : WICBitmapInterpolationModeFant)))
                return false;
            std::vector<uint8_t> scaled(scaledBytes);
            if (FAILED(scaler->CopyPixels(nullptr, plan.width * 4, UINT(scaledBytes), scaled.data()))) return false;
            for (size_t i = 3; i < scaledBytes; i += 4) scaled[i] = 255;
            bgra.swap(scaled);
        }
        picture.width = plan.width; picture.height = plan.height;
        picture.fileWidth = fileWidth; picture.fileHeight = fileHeight;
        picture.hdr = hdr;
        picture.bgra = std::move(bgra);
        error.clear();
        return true;
    } catch (...) {
        picture = {};
        error = L"failed";
        return false;
    }
}

std::vector<ocr::Language> languages() {
    std::vector<ocr::Language> out;
    try {
        Apartment apartment;
        for (const auto& language : OcrEngine::AvailableRecognizerLanguages())
            out.push_back({std::wstring(language.LanguageTag()), std::wstring(language.DisplayName())});
    } catch (...) { out.clear(); }
    return out;
}

Result readFile(const std::wstring& path, const std::wstring& languageTag, Region region) {
    Result result;
    try {
        Apartment apartment;
        OcrEngine engine{nullptr};
        if (languageTag.empty()) engine = OcrEngine::TryCreateFromUserProfileLanguages();
        else if (winrt::Windows::Globalization::Language::IsWellFormed(languageTag)) {
            const winrt::Windows::Globalization::Language language{languageTag};
            if (OcrEngine::IsLanguageSupported(language)) engine = OcrEngine::TryCreateFromLanguage(language);
        }
        if (!engine) { result.error = L"no-language"; return result; }
        result.language = std::wstring(engine.RecognizerLanguage().LanguageTag());

        Picture picture;
        if (!loadPicture(path, region, OcrEngine::MaxImageDimension(), picture, result.error)) return result;
        result.width = picture.width; result.height = picture.height; result.hdr = picture.hdr;
        DataWriter writer;
        writer.WriteBytes(winrt::array_view<const uint8_t>(picture.bgra.data(), picture.bgra.data() + picture.bgra.size()));
        const auto buffer = writer.DetachBuffer();
        const SoftwareBitmap bitmap = SoftwareBitmap::CreateCopyFromBuffer(buffer, BitmapPixelFormat::Bgra8, int32_t(picture.width),
                                                                           int32_t(picture.height), BitmapAlphaMode::Premultiplied);
        const auto read = engine.RecognizeAsync(bitmap).get();
        const double fullWidth = double(picture.width), fullHeight = double(picture.height);
        for (const auto& line : read.Lines()) {
            Line out;
            out.text = std::wstring(line.Text());
            double left = fullWidth, top = fullHeight, right = 0, bottom = 0;
            for (const auto& word : line.Words()) {
                const Rect box = word.BoundingRect();
                left = std::min(left, double(box.X)); top = std::min(top, double(box.Y));
                right = std::max(right, double(box.X) + double(box.Width)); bottom = std::max(bottom, double(box.Y) + double(box.Height));
            }
            if (right > left && bottom > top) {
                out.x = std::clamp(left / fullWidth, 0.0, 1.0); out.y = std::clamp(top / fullHeight, 0.0, 1.0);
                out.w = std::clamp((right - left) / fullWidth, 0.0, 1.0 - out.x); out.h = std::clamp((bottom - top) / fullHeight, 0.0, 1.0 - out.y);
            }
            if (!out.text.empty()) result.lines.push_back(std::move(out));
        }
        result.error.clear();
        result.ok = true;
    } catch (...) {
        result.ok = false; result.lines.clear(); result.error = L"failed";
    }
    return result;
}

}
