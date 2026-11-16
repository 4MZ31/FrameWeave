// SPDX-License-Identifier: GPL-3.0-or-later
// GPU check scene (PushFPS --gpucheck): one FrameView filling the window, neutral picture settings.
import QtQuick
import PushFPS

Rectangle {
    color: "black"
    FrameView {
        anchors.fill: parent
    }
}
