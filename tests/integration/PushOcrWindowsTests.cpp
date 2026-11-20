// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Reading the text on a picture with Windows' own recogniser (ui/PushOcr.h, src/ui/PushOcrWinrt.cpp),
// on pictures drawn here: two lines of black text on white, saved as a bitmap file, and the same as
// an HDR frame is saved (linear half floats in a JPEG XR file) with text and background both
// brighter than white, which is the case that is lost when such a picture is cut off at white.
//
// Checked on every machine: the picture is cut, scaled and brought down as ui/PushOcr.h says, pixel
// values included. A machine without a text recognition language cannot read anything: the test then
// says so and passes. Where a language is installed, the words drawn have to come back, from both.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "veyra/ui/PushOcr.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

using namespace veyra::ui::ocr;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::printf("FAILED line %d: %s\n", __LINE__, #x); } } while (0)

static std::wstring upper(std::wstring text) { for (auto& c : text) c = wchar_t(std::towupper(c)); return text; }
static bool holds(const std::wstring& text, const wchar_t* word) { return upper(text).find(word) != std::wstring::npos; }
static std::string narrow(const std::wstring& text) {
    std::string out;
    for (const wchar_t c : text) out += c < 128 ? char(c) : '?';
    return out;
}

// White, with one line in its upper half and one in its lower half. `pixels` gets the rows top-down,
// four bytes a pixel (blue, green, red, unused).
static bool draw(int width, int height, std::vector<unsigned char>& pixels) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;   // top-down
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    const HDC screen = GetDC(nullptr);
    const HDC dc = CreateCompatibleDC(screen);
    const HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bitmap || !bits) return false;
    const HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    RECT all{0, 0, width, height};
    FillRect(dc, &all, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    const HFONT font = CreateFontW(height * 110 / 480, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
    const HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    RECT top{60, height * 50 / 480, width, height * 230 / 480}, bottom{60, height * 290 / 480, width, height * 470 / 480};
    DrawTextW(dc, L"FRAME GENERATION 1234", -1, &top, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(dc, L"CONTINUE SAVED GAME", -1, &bottom, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();
    pixels.assign(static_cast<unsigned char*>(bits), static_cast<unsigned char*>(bits) + size_t(width) * size_t(height) * 4);
    SelectObject(dc, oldFont); DeleteObject(font);
    SelectObject(dc, oldBitmap); DeleteObject(bitmap);
    DeleteDC(dc); ReleaseDC(nullptr, screen);
    return true;
}

// A bitmap file: the two headers and the pixels (bottom-up, as the format has them).
static bool writeBitmap(const std::wstring& path, int width, int height, const std::vector<unsigned char>& pixels) {
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER header{};
    header.biSize = sizeof header; header.biWidth = width; header.biHeight = height;
    header.biPlanes = 1; header.biBitCount = 32; header.biCompression = BI_RGB;
    const DWORD bytes = DWORD(width) * DWORD(height) * 4;
    file.bfType = 0x4D42; file.bfOffBits = sizeof file + sizeof header; file.bfSize = file.bfOffBits + bytes;
    std::vector<unsigned char> flipped(bytes);
    for (int y = 0; y < height; ++y)
        memcpy(flipped.data() + size_t(height - 1 - y) * width * 4, pixels.data() + size_t(y) * width * 4, size_t(width) * 4);
    bool written = false;
    FILE* out = nullptr;
    if (_wfopen_s(&out, path.c_str(), L"wb") == 0 && out) {
        written = fwrite(&file, sizeof file, 1, out) == 1 && fwrite(&header, sizeof header, 1, out) == 1 && fwrite(flipped.data(), 1, bytes, out) == bytes;
        fclose(out);
    }
    return written;
}

// The same picture as PushFPS saves an HDR frame: linear half floats, 1 = 80 nits, JPEG XR without
// loss. Black becomes `dark` and white becomes `bright`, both in the file's units.
static bool writeHdr(const std::wstring& path, int width, int height, const std::vector<unsigned char>& pixels, float dark, float bright) {
    using Microsoft::WRL::ComPtr;
    std::vector<uint16_t> half(size_t(width) * size_t(height) * 4);
    for (size_t i = 0; i < size_t(width) * size_t(height); ++i) {
        for (size_t c = 0; c < 3; ++c)   // the file has red first, the drawing blue first
            half[i * 4 + c] = floatToHalf(dark + (bright - dark) * float(pixels[i * 4 + (2 - c)]) / 255.0f);
        half[i * 4 + 3] = floatToHalf(1.0f);
    }
    ComPtr<IWICImagingFactory> factory; ComPtr<IWICStream> stream; ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> options;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return false;
    if (FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatWmp, nullptr, &encoder)) || FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;
    if (FAILED(encoder->CreateNewFrame(&frame, &options))) return false;
    PROPBAG2 property{}; property.pstrName = const_cast<wchar_t*>(L"Lossless");
    VARIANT lossless{}; lossless.vt = VT_BOOL; lossless.boolVal = VARIANT_TRUE;
    if (FAILED(options->Write(1, &property, &lossless)) || FAILED(frame->Initialize(options.Get())) || FAILED(frame->SetSize(UINT(width), UINT(height)))) return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat64bppRGBAHalf;
    if (FAILED(frame->SetPixelFormat(&format)) || !IsEqualGUID(format, GUID_WICPixelFormat64bppRGBAHalf)) return false;
    if (FAILED(frame->WritePixels(UINT(height), UINT(width) * 8, UINT(half.size() * 2), reinterpret_cast<BYTE*>(half.data())))) return false;
    return SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

int main() {
    wchar_t folder[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, folder)) { std::printf("no temporary folder\n"); return 1; }
    const std::wstring path = std::wstring(folder) + L"pushfps-read-test.bmp";
    const std::wstring hdrPath = std::wstring(folder) + L"pushfps-read-test.jxr";
    DeleteFileW(path.c_str()); DeleteFileW(hdrPath.c_str());
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    // 1600 x 480: lower than 720, so it is read at twice its size. 1600 x 800 for the HDR one: read as it is.
    std::vector<unsigned char> drawn, drawnTall;
    CHECK(draw(1600, 480, drawn) && writeBitmap(path, 1600, 480, drawn));
    const float dark = 3.0f, bright = 10.0f;   // 240 nits on 800: both above white
    CHECK(draw(1600, 800, drawnTall) && writeHdr(hdrPath, 1600, 800, drawnTall, dark, bright));

    // --- The picture as the recogniser gets it (no language needed) ---
    {
        Picture picture; std::wstring error;
        CHECK(loadPicture(path, regionFor(0), 10000, picture, error) && error.empty());
        CHECK(picture.fileWidth == 1600 && picture.fileHeight == 480 && picture.width == 3200 && picture.height == 960 && !picture.hdr);
        CHECK(picture.bgra.size() == size_t(3200) * 960 * 4);
        if (picture.bgra.size() == size_t(3200) * 960 * 4) {
            CHECK(picture.bgra[0] == 255 && picture.bgra[1] == 255 && picture.bgra[2] == 255 && picture.bgra[3] == 255);   // a white corner
            size_t darkPixels = 0; bool opaque = true;
            for (size_t i = 0; i < picture.bgra.size(); i += 4) { if (picture.bgra[i + 1] < 64) ++darkPixels; opaque = opaque && picture.bgra[i + 3] == 255; }
            std::printf("bitmap: %ux%u read at %ux%u, dark pixels %zu\n", picture.fileWidth, picture.fileHeight, picture.width, picture.height, darkPixels);
            CHECK(opaque && darkPixels > 20000 && darkPixels < picture.bgra.size() / 4 / 3);   // text, and mostly paper
        }
        // The lower third, cut out and doubled: 160 rows of 480.
        CHECK(loadPicture(path, regionFor(2), 10000, picture, error) && picture.width == 3200 && picture.height == 320);
        // Never larger than the recogniser takes.
        CHECK(loadPicture(path, regionFor(0), 800, picture, error) && picture.width == 800 && picture.height == 240);
        // A file that is not there, and one that is no picture.
        CHECK(!loadPicture(std::wstring(folder) + L"pushfps-read-test-not-there.bmp", regionFor(0), 10000, picture, error) && error == L"no-file" && picture.bgra.empty());
        const std::wstring junk = std::wstring(folder) + L"pushfps-read-test-junk.png";
        FILE* out = nullptr;
        if (_wfopen_s(&out, junk.c_str(), L"wb") == 0 && out) { fputs("this is not a picture", out); fclose(out); }
        CHECK(!loadPicture(junk, regionFor(0), 10000, picture, error) && error == L"failed");
        DeleteFileW(junk.c_str());
    }
    {
        // The HDR frame: nothing is cut off at white. The paper is the brightest value and becomes 255,
        // the ink becomes exactly what the header's rule says, and the two stay far apart.
        Picture picture; std::wstring error;
        CHECK(loadPicture(hdrPath, regionFor(0), 10000, picture, error) && error.empty());
        CHECK(picture.hdr && picture.width == 1600 && picture.height == 800 && picture.fileHeight == 800);
        if (picture.hdr && picture.bgra.size() == size_t(1600) * 800 * 4) {
            const float peak = hdrPeak(floatToHalf(bright)), low = hdrFloor(floatToHalf(dark), peak);
            const int paper = hdrByte(halfToFloat(floatToHalf(bright)), low, peak), ink = hdrByte(halfToFloat(floatToHalf(dark)), low, peak);
            int lowest = 255, highest = 0;
            for (size_t i = 0; i < picture.bgra.size(); i += 4)
                for (size_t c = 0; c < 3; ++c) { lowest = std::min(lowest, int(picture.bgra[i + c])); highest = std::max(highest, int(picture.bgra[i + c])); }
            std::printf("hdr: paper %d ink %d, read lowest %d highest %d\n", paper, ink, lowest, highest);
            CHECK(paper == 255 && picture.bgra[0] == 255 && highest == 255);
            CHECK(lowest == ink && paper - ink >= 190);
        }
    }

    // What can be checked on every machine: nothing throws, and what is missing is said.
    const auto missing = readFile(std::wstring(folder) + L"pushfps-read-test-not-there.bmp", L"", regionFor(0));
    CHECK(!missing.ok && missing.lines.empty());
    const auto known = languages();
    std::printf("recognition languages on this machine: %zu, largest side read: %u\n", known.size(), maxDimension());
    for (const auto& language : known) std::printf("  %s\n", narrow(language.tag).c_str());
    if (known.empty()) {
        const auto none = readFile(path, L"", regionFor(0));
        CHECK(!none.ok && none.error == L"no-language");
        DeleteFileW(path.c_str()); DeleteFileW(hdrPath.c_str());
        if (com) CoUninitialize();
        std::printf(failures ? "%d failed\n" : "SKIPPED the reading itself: Windows has no text recognition language on this machine (%d failed)\n", failures);
        return failures ? 1 : 0;
    }
    CHECK(missing.error == L"no-file");

    // The whole picture: both lines, in order.
    const auto whole = readFile(path, known.front().tag, regionFor(0));
    std::printf("whole: ok=%d language=%s lines=%zu text=[%s]\n", whole.ok, narrow(whole.language).c_str(), whole.lines.size(), narrow(joinLines(whole.lines)).c_str());
    CHECK(whole.ok && whole.error.empty() && !whole.hdr && whole.width == 3200 && whole.height == 960);
    CHECK(whole.lines.size() >= 2);
    const auto text = joinLines(whole.lines);
    CHECK(holds(text, L"FRAME") && holds(text, L"GENERATION") && holds(text, L"1234"));
    CHECK(holds(text, L"CONTINUE") && holds(text, L"SAVED"));
    CHECK(upper(text).find(L"FRAME") < upper(text).find(L"CONTINUE"));
    for (const auto& line : whole.lines) CHECK(line.w > 0 && line.h > 0 && line.x >= 0 && line.y >= 0 && line.x + line.w <= 1.0001 && line.y + line.h <= 1.0001);

    // The lower half: the second line and not the first.
    const auto lower = readFile(path, known.front().tag, regionFor(1));
    std::printf("lower half: ok=%d text=[%s]\n", lower.ok, narrow(joinLines(lower.lines)).c_str());
    CHECK(lower.ok);
    CHECK(holds(joinLines(lower.lines), L"CONTINUE") && !holds(joinLines(lower.lines), L"GENERATION"));
    // The upper half: the first and not the second.
    const auto upperHalf = readFile(path, known.front().tag, regionFor(3));
    CHECK(upperHalf.ok && holds(joinLines(upperHalf.lines), L"GENERATION") && !holds(joinLines(upperHalf.lines), L"CONTINUE"));

    // The user's own languages (no tag), and a language that is not installed.
    const auto own = readFile(path, L"", regionFor(0));
    std::printf("own languages: ok=%d error=%s\n", own.ok, narrow(own.error).c_str());
    CHECK(own.ok || own.error == L"no-language");
    const auto absent = readFile(path, L"zu-ZA", regionFor(0));
    CHECK(absent.ok || absent.error == L"no-language" || absent.error == L"failed");
    const auto nonsense = readFile(path, L"not a language tag !!", regionFor(0));
    CHECK(!nonsense.ok && nonsense.error == L"no-language");   // refused, not thrown

    // The HDR frame: both lines, though ink and paper are both brighter than white.
    const auto hdr = readFile(hdrPath, known.front().tag, regionFor(0));
    std::printf("hdr: ok=%d lines=%zu text=[%s]\n", hdr.ok, hdr.lines.size(), narrow(joinLines(hdr.lines)).c_str());
    CHECK(hdr.ok && hdr.hdr && hdr.width == 1600 && hdr.height == 800);
    CHECK(holds(joinLines(hdr.lines), L"GENERATION") && holds(joinLines(hdr.lines), L"CONTINUE"));

    DeleteFileW(path.c_str()); DeleteFileW(hdrPath.c_str());
    if (com) CoUninitialize();
    if (failures) { std::printf("%d failed\n", failures); return 1; }
    std::printf("PushOcrWindowsTests: all passed\n");
    return 0;
}
