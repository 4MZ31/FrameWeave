// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#include "veyra/ui/PushSourceImageProvider.h"

#include "veyra/engine/PushLayerSources.h"

#include <QImage>

#include <algorithm>

namespace veyra::ui {

PushSourceImageProvider::PushSourceImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage PushSourceImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    // "<source id>/<serial>": the serial only makes the address new.
    const QString sourceId = id.section(QLatin1Char('/'), 0, 0);
    const auto frame = engine::pushSourceFrame(sourceId.toStdString());
    if (!frame || frame->width < 2 || frame->height < 2 || frame->rgba.size() < size_t(frame->width) * frame->height * 4) {
        QImage none(2, 2, QImage::Format_ARGB32);
        none.fill(Qt::transparent);
        if (size) *size = none.size();
        return none;
    }
    // The frame's own memory, alive while `frame` is; every path below returns a copy.
    const QImage whole(frame->rgba.data(), int(frame->width), int(frame->height), int(frame->width) * 4, QImage::Format_RGBA8888);
    const int wanted = requestedSize.width() > 0 ? std::min(requestedSize.width(), int(frame->width)) : int(frame->width);
    QImage result;
    if (wanted >= int(frame->width)) {
        result = whole.copy();
    } else {
        const int height = std::max(2, int(qint64(frame->height) * wanted / frame->width));
        // Far larger than asked: a quick pass to twice the size first, then the smooth one.
        if (int(frame->width) >= wanted * 4)
            result = whole.scaled(wanted * 2, height * 2, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                          .scaled(wanted, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        else result = whole.scaled(wanted, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    if (size) *size = result.size();
    return result;
}

} // namespace veyra::ui
