// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The PushFPS wordmark as in the logo: a bold, slanted, slightly condensed
// face, "Push" in white (dark text in the light theme) and "FPS" in the logo's
// green. The name comes from the bridge (Brand.h), split before its last three
// letters. Bahnschrift ships with Windows 10 and 11; elsewhere the UI font is used.
import QtQuick

Row {
    id: mark
    objectName: "pushfps-wordmark"
    property string name: (typeof veyra !== "undefined" && veyra) ? veyra.appName : "PushFPS"
    property real pixelSize: 30
    readonly property bool split: name.length > 3 && name.slice(-3) === "FPS"
    readonly property string head: split ? name.slice(0, -3) : name
    readonly property string tail: split ? "FPS" : ""
    readonly property string family: Qt.fontFamilies().indexOf("Bahnschrift") >= 0 ? "Bahnschrift" : Theme.fontUi
    readonly property color headColor: Theme.light ? Theme.t1 : "#FFFFFF"
    spacing: 0
    Accessible.role: Accessible.Heading
    Accessible.name: name

    Text {
        objectName: "pushfps-wordmark-push"
        text: mark.head
        color: mark.headColor
        font.family: mark.family
        font.pixelSize: mark.pixelSize
        font.weight: Font.Bold
        font.italic: true
        font.letterSpacing: -0.02 * mark.pixelSize
    }
    Text {
        objectName: "pushfps-wordmark-fps"
        visible: mark.tail.length > 0
        text: mark.tail
        color: Theme.brandGreen
        font.family: mark.family
        font.pixelSize: mark.pixelSize
        font.weight: Font.Bold
        font.italic: true
        font.letterSpacing: -0.02 * mark.pixelSize
    }
}
