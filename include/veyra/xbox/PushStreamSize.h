// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The picture size asked of Xbox Remote Play (4.5.3). The service sizes the stream from the client's
// display it is told about, and PushFPS always said 1920 x 1080. Automatic sends exactly what it always
// sent, byte for byte; 1080p and 1440p say that display size in both places the service reads it (the
// device header of the play request and the dimensions message on the WebRTC channel). Whether a console
// and the service grant 1440p is theirs to decide: a stream that comes back smaller plays as it comes.
// Pure: tested with g++.
#pragma once
#include <string>

namespace veyra::xbox {

enum class PushStreamSize { Auto = 0, FullHd = 1080, QuadHd = 1440 };

// A stored value (0, 1080, 1440); anything else is Automatic.
inline PushStreamSize pushStreamSizeFrom(int value) {
    return value == 1080 ? PushStreamSize::FullHd : value == 1440 ? PushStreamSize::QuadHd : PushStreamSize::Auto;
}

struct PushStreamExtent { unsigned width = 1920, height = 1080; };
// The display size told to the service. Automatic is the 1920 x 1080 PushFPS always sent.
inline PushStreamExtent pushStreamExtent(PushStreamSize size) {
    return size == PushStreamSize::QuadHd ? PushStreamExtent{2560, 1440} : PushStreamExtent{1920, 1080};
}

// The device header for the play request: `base` untouched for Automatic; otherwise its display size
// written in. A header without the expected display size is sent as it is.
inline std::string pushDeviceInfo(const std::string& base, PushStreamSize size) {
    if (size == PushStreamSize::Auto) return base;
    static const std::string kFrom = "\"widthInPixels\":1920,\"heightInPixels\":1080";
    const auto at = base.find(kFrom);
    if (at == std::string::npos) return base;
    const auto e = pushStreamExtent(size);
    std::string out = base;
    out.replace(at, kFrom.size(), "\"widthInPixels\":" + std::to_string(e.width) + ",\"heightInPixels\":" + std::to_string(e.height));
    return out;
}

} // namespace veyra::xbox
