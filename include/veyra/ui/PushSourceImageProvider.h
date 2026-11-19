#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS streaming mode: a scene source's newest frame for the interface (the source's settings and
// the layout editor), as `image://push-source/<source id>/<serial>`. The serial is
// QmlPlayerBridge::pushSourceSerial: it changes with every frame the source publishes, so an Image
// bound to it shows the camera at the camera's own rate. Until 3.2.3 these previews were a JPEG
// data URL fetched once or twice a second, which made every camera look like a slide show.
#include <QQuickImageProvider>

namespace veyra::ui {

class PushSourceImageProvider final : public QQuickImageProvider {
public:
    PushSourceImageProvider();
    // Scaled to requestedSize's width when one is given (the Image's sourceSize); straight alpha
    // kept, so rounded and round sources show their shape. A source without a frame gives a clear
    // 2 x 2 picture rather than a provider error.
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

} // namespace veyra::ui
