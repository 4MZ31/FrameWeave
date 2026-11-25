// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
import QtQuick

Text {
    id: mark
    objectName: "pushfps-wordmark"
    property string name: (typeof veyra !== "undefined" && veyra) ? veyra.appName : "FrameWeave"
    property real pixelSize: 30
    readonly property bool split: name === "FrameWeave"
    readonly property string head: split ? "Frame" : name
    readonly property string tail: split ? "Weave" : ""
    readonly property string family: Qt.fontFamilies().indexOf("Bahnschrift") >= 0 ? "Bahnschrift" : Theme.fontUi
    readonly property color headColor: Theme.light ? Theme.t1 : "#FFFFFF"
    readonly property color tailColor: Theme.brandGreen
    // One run of text, so the two halves keep normal spacing between them.
    textFormat: Text.StyledText
    text: tail.length > 0 ? head + "<font color=\"" + tailColor + "\">" + tail + "</font>" : head
    color: headColor
    font.family: family
    font.pixelSize: pixelSize
    font.weight: Font.Bold
    font.italic: true
    font.letterSpacing: -0.02 * pixelSize
    Accessible.role: Accessible.Heading
    Accessible.name: name
}
