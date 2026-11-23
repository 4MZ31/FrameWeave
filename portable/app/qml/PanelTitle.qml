// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The title of a card, with an optional line under it.
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    property string text: ""
    property string hint: ""
    Layout.fillWidth: true
    Layout.bottomMargin: 2
    spacing: 2
    Text {
        Layout.fillWidth: true
        text: parent.text
        elide: Text.ElideRight
        color: Theme.t1
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsH3
        font.weight: Font.DemiBold
        font.variableAxes: Theme.axesDemiBold
    }
    Text {
        visible: parent.hint.length > 0
        Layout.fillWidth: true
        text: parent.hint
        wrapMode: Text.WordWrap
        color: Theme.t3
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsHint
    }
}
