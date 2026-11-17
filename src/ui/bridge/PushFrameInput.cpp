// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS Frame Input API (docs/FRAME-INPUT-API.md): list the applications sending frames and open one
// as the live source.
#include "../QmlPlayerBridge_p.h"
#include "veyra/source/FrameInputSource.h"
#include "veyra/Log.h"

#include <format>

namespace veyra::ui {

QVariantList QmlPlayerBridge::pushFrameInputSenders() {
    QVariantList out;
    for (const auto& [channel, s] : source::FrameInputChannel::discover())
        out.push_back(QVariantMap{{"channel", QString::fromStdWString(channel)}, {"name", QString::fromUtf8(s.name.c_str())},
                                  {"width", int(s.maxWidth)}, {"height", int(s.maxHeight)},
                                  {"fps", s.fpsDen ? double(s.fpsNum) / double(s.fpsDen) : 0.0}, {"alive", s.alive},
                                  {"frames", qulonglong(s.frames)}, {"pid", int(s.pid)}});
    return out;
}

bool QmlPlayerBridge::pushStartFrameInput(const QString& channel) {
    const std::wstring name = channel.isEmpty() ? std::wstring(L"default") : channel.toStdWString();
    if (!source::validFrameInputChannel(name)) { emit notice(tr("画面输入通道名称无效"), true); return false; }
    source::FrameInputChannel probe;
    if (!probe.open(name)) { emit notice(tr("没有应用在这个通道发送画面"), true); return false; }
    const auto sender = probe.sender();
    probe.close();
    const QString label = sender.name.empty() ? QString::fromStdWString(name) : QString::fromUtf8(sender.name.c_str());
    veyra::log::info("frame-input-ui", std::format("start channel={} sender=\"{}\"", channel.toStdString(), sender.name));
    impl_->liveKind = QStringLiteral("frameinput");
    impl_->liveLabel = label;
    openSourceUri(source::frameInputUri(name), label.toStdWString(), false);
    return true;
}

} // namespace veyra::ui
