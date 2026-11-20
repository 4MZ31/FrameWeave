// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// On-screen latency readout (preference pushLatencyOverlay). Measured, not
// estimated: the engine times every real frame from the capture card's callback
// to the return of Present (mean and 95th percentile over ten seconds), and counts
// the frames handed to the display (frame rate and its 1% low). It is the PC's
// share of the delay; the capture card, the display and the game add their own,
// which software cannot see - the quick setup hint says so.
//
// Since 4.0 it can be dragged anywhere over the picture. Where it sits is kept as two shares of the room
// it has there (preferences pushLatencyX / pushLatencyY, thousandths; none = the top left corner, where
// it always was), so it keeps its place when the window changes size or goes full screen, and can never
// end up outside the picture. A double click sends it back to the corner. The page gives it the
// picture's rectangle (areaX ... areaHeight); without one it stays where its x and y put it.
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: badge
    objectName: "pushfps-latency-badge"
    property var stats: ({})
    readonly property bool wanted: veyra.preferences.pushLatencyOverlay === true
    readonly property bool hasNumbers: stats.running === true && stats.latencyMs !== undefined && stats.latencyMs !== null
    visible: wanted && stats.running === true
    readonly property bool videoCover: visible
    readonly property real coverRadius: 9
    implicitWidth: row.implicitWidth + 20
    implicitHeight: 30
    radius: 9
    color: Qt.rgba(0.04, 0.05, 0.04, 0.82)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.10)

    // --- where it sits ------------------------------------------------------------------------------
    property real areaX: 0
    property real areaY: 0
    property real areaWidth: 0
    property real areaHeight: 0
    readonly property real edge: 12                       // kept clear of the picture's edge
    readonly property real roomX: Math.max(0, areaWidth - width - 2 * edge)
    readonly property real roomY: Math.max(0, areaHeight - height - 2 * edge)
    function share(key) {
        const value = veyra.preferences[key]
        const number = Number(value)
        return value === undefined || value === null || !isFinite(number) ? 0 : Math.max(0, Math.min(1, number / 1000))
    }
    readonly property real shareX: share("pushLatencyX")
    readonly property real shareY: share("pushLatencyY")
    // Never while mouse input goes to the game: a click there is the game's.
    readonly property bool movable: areaWidth > 0 && areaHeight > 0 && veyra.gameInputActive !== true
    property bool dragging: false
    property real dragX: 0
    property real dragY: 0
    function clampX(value) { return Math.max(areaX + edge, Math.min(areaX + edge + roomX, value)) }
    function clampY(value) { return Math.max(areaY + edge, Math.min(areaY + edge + roomY, value)) }
    x: dragging ? dragX : areaX + edge + shareX * roomX
    y: dragging ? dragY : areaY + edge + shareY * roomY
    function moveTo(left, top) { dragX = clampX(left); dragY = clampY(top); dragging = true }
    // The place is written before the drag ends, so the badge never jumps back for a frame.
    function drop() {
        if (!dragging) return
        veyra.setPreference("pushLatencyX", roomX > 0 ? Math.round(1000 * (dragX - areaX - edge) / roomX) : 0)
        veyra.setPreference("pushLatencyY", roomY > 0 ? Math.round(1000 * (dragY - areaY - edge) / roomY) : 0)
        dragging = false
    }
    function toCorner() {
        dragging = false
        veyra.setPreference("pushLatencyX", 0)
        veyra.setPreference("pushLatencyY", 0)
    }
    MouseArea {
        id: grip
        objectName: "pushfps-latency-drag"
        anchors.fill: parent
        enabled: badge.movable
        hoverEnabled: enabled
        preventStealing: true
        cursorShape: badge.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property real holdX: 0      // where on the badge it was taken hold of
        property real holdY: 0
        onPressed: mouse => { holdX = mouse.x; holdY = mouse.y }
        onPositionChanged: mouse => {
            if (!pressed) return
            const p = mapToItem(badge.parent, mouse.x, mouse.y)
            const left = p.x - holdX, top = p.y - holdY
            // A few pixels of slack, so a click or a double click is not taken for a drag.
            if (!badge.dragging && Math.abs(left - badge.x) + Math.abs(top - badge.y) < 4) return
            badge.moveTo(left, top)
        }
        onReleased: badge.drop()
        onCanceled: badge.dragging = false
        onDoubleClicked: badge.toCorner()
    }

    function refresh() { badge.stats = veyra.pushLiveStats() || {} }
    Timer { interval: 500; repeat: true; running: badge.wanted; triggeredOnStart: true; onTriggered: badge.refresh() }

    // Green under 30 ms of PC latency, amber to 60, red above.
    readonly property color level: !hasNumbers ? Theme.t3 : stats.latencyMs < 30 ? Theme.accent : stats.latencyMs < 60 ? Theme.warn : "#FF6B5B"

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 10
        Rectangle { implicitWidth: 7; implicitHeight: 7; radius: 3.5; color: badge.level }
        Text {
            objectName: "pushfps-latency-ms"
            text: badge.hasNumbers ? qsTr("延迟 %1 ms").arg(Number(badge.stats.latencyMs).toFixed(0)) : qsTr("延迟 测量中")
            color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: 12; font.weight: Font.DemiBold
        }
        Text {
            visible: badge.hasNumbers && badge.stats.latencyP95Ms !== undefined && badge.stats.latencyP95Ms !== null
            text: qsTr("p95 %1").arg(Number(badge.stats.latencyP95Ms).toFixed(0))
            color: Theme.t3; font.family: Theme.fontMono; font.pixelSize: 11
        }
        Text {
            visible: badge.stats.submitFps !== undefined && badge.stats.submitFps !== null
            text: qsTr("%1 FPS").arg(Number(badge.stats.submitFps).toFixed(0))
                  + (badge.stats.submitLowFps !== undefined && badge.stats.submitLowFps !== null ? qsTr(" · 1% %1").arg(Number(badge.stats.submitLowFps).toFixed(0)) : "")
            color: Theme.t2; font.family: Theme.fontMono; font.pixelSize: 11
        }
    }
}
