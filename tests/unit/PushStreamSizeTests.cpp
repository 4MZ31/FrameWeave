// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// The Xbox Remote Play picture size (xbox/PushStreamSize.h): Automatic sends exactly what was always sent.
#include "veyra/xbox/PushStreamSize.h"
#include <cstdio>
#include <string>

using namespace veyra::xbox;
static int failures = 0;
static void check(bool ok, const char* label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label); if (!ok) ++failures; }

int main() {
    // The header PushFPS always sends (src/xbox/StreamApi.cpp, kDeviceInfo), in the part that matters.
    const std::string base = R"({"dev":{"displayInfo":{"dimensions":{"widthInPixels":1920,"heightInPixels":1080},"pixelDensity":{"dpiX":1,"dpiY":1}}}})";
    check(pushDeviceInfo(base, PushStreamSize::Auto) == base, "Automatic sends the header byte for byte");
    check(pushDeviceInfo(base, PushStreamSize::FullHd) == base, "1080p is the size always sent");
    check(pushDeviceInfo(base, PushStreamSize::QuadHd) ==
          R"({"dev":{"displayInfo":{"dimensions":{"widthInPixels":2560,"heightInPixels":1440},"pixelDensity":{"dpiX":1,"dpiY":1}}}})",
          "1440p writes 2560 x 1440 into the header and nothing else");
    check(pushDeviceInfo("{}", PushStreamSize::QuadHd) == "{}", "a header without the size is sent as it is");
    check(pushStreamExtent(PushStreamSize::Auto).width == 1920 && pushStreamExtent(PushStreamSize::Auto).height == 1080,
          "Automatic tells the console 1920 x 1080, as always");
    check(pushStreamExtent(PushStreamSize::QuadHd).width == 2560 && pushStreamExtent(PushStreamSize::QuadHd).height == 1440, "1440p is 2560 x 1440");
    check(pushStreamSizeFrom(0) == PushStreamSize::Auto && pushStreamSizeFrom(1080) == PushStreamSize::FullHd &&
          pushStreamSizeFrom(1440) == PushStreamSize::QuadHd && pushStreamSizeFrom(2160) == PushStreamSize::Auto && pushStreamSizeFrom(-1) == PushStreamSize::Auto,
          "a stored value other than 0, 1080 or 1440 is Automatic");
    std::printf(failures ? "%d FAILED\n" : "ALL PASSED (%d failures)\n", failures);
    return failures ? 1 : 0;
}
