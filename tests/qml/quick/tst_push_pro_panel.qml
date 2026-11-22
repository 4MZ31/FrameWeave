// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Professional mode's panel (4.2.1): the panel over the full-screen picture is as wide as its right edge
// is dragged, and the quick setup stands there as two cards, the Picture section in the Quality tab and
// the rest in the Frame Gen tab.
import QtQuick
import QtTest
import Veyra

Item {
    id: root
    width: 1600; height: 900
    ProPage { id: page; anchors.fill: parent; visible: true; enabled: true; tab: "fg" }

    TestCase {
        name: "PushProPanel"
        when: windowShown
        property var savedPreferences: ({})
        function init() { savedPreferences = veyra.preferences; veyra.preferences = ({}); page.overlay = false; page.tab = "fg" }
        function cleanup() { page.overlay = false; page.tab = "fg"; veyra.preferences = savedPreferences }
        function control(parent, name) { const c = findChild(parent, name); verify(c !== null, name); return c }

        function test_overlay_panel_is_as_wide_as_its_edge_is_dragged() {
            const panel = control(page, "pro-inspector"), grip = control(page, "pro-overlay-split")
            verify(!grip.visible, "docked beside the picture: the splitter between them is the handle")
            const docked = panel.width
            page.overlay = true
            tryVerify(function() { return grip.visible && grip.height > 300 }, 3000, "over the picture the panel's own edge is")
            compare(page.overlayWidth, 376, "the width it always had until it is dragged")
            compare(panel.width, 376 + page.overlayGrip)
            verify(page.overlayContains(panel.x + panel.width - 2, panel.y + 100), "a press on the grip belongs to the panel, not the picture")
            // Drag the edge 150 pixels to the right.
            const start = grip.mapToItem(page, grip.width / 2, 200)
            mousePress(page, start.x, start.y)
            mouseMove(page, start.x + 60, start.y)
            compare(Math.round(page.overlayWidth), 436, "the panel follows the mouse")
            mouseMove(page, start.x + 150, start.y)
            compare(Math.round(panel.width), 526 + page.overlayGrip)
            verify(veyra.preferences.pushOverlayPanelWidth === undefined, "nothing is saved while it is dragged")
            mouseRelease(page, start.x + 150, start.y)
            compare(veyra.preferences.pushOverlayPanelWidth, 526, "the width is remembered")
            compare(Math.round(page.overlayWidth), 526)
            // Never narrower than it was, never wider than leaves the picture room.
            const again = grip.mapToItem(page, grip.width / 2, 200)
            mousePress(page, again.x, again.y)
            mouseMove(page, again.x - 400, again.y)
            compare(page.overlayWidth, 376)
            mouseMove(page, again.x + 500, again.y)
            compare(page.overlayWidth, page.overlayMax)
            verify(page.overlayMax <= page.width * 0.6 + 0.5 && page.overlayMax >= 376)
            mouseRelease(page, again.x + 500, again.y)
            compare(veyra.preferences.pushOverlayPanelWidth, Math.round(page.overlayMax))
            // A double click puts it back, and its own release does not undo that.
            const edge = grip.mapToItem(page, grip.width / 2, 200)
            mouseDoubleClickSequence(page, edge.x, edge.y)
            compare(veyra.preferences.pushOverlayPanelWidth, 376)
            compare(page.overlayWidth, 376)
            // A saved width larger than a smaller window allows is held to the limit, not lost.
            veyra.setPreference("pushOverlayPanelWidth", 4000)
            compare(page.overlayWidth, page.overlayMax)
            compare(veyra.preferences.pushOverlayPanelWidth, 4000)
            // Docked again: its width is its own, as it was.
            page.overlay = false
            tryVerify(function() { return !grip.visible && Math.abs(panel.width - docked) < 1 }, 3000, "the docked panel keeps its own width")
        }
        function test_the_picture_settings_stand_in_the_quality_tab() {
            const picture = control(page, "list-pushfps-picture"), rest = control(page, "list-pushfps-quick")
            compare(picture.scope, "picture")
            compare(rest.scope, "rest")
            verify(picture.proPage && rest.proPage)
            page.tab = "quality"
            tryVerify(function() { return picture.visible && !rest.visible }, 3000)
            verify(control(picture, "pushfps-section-picture").visible && control(picture, "pushfps-crop-row").visible, "the Picture section, with the page's own enhancement card")
            verify(!control(picture, "pushfps-tools-row").visible && !control(picture, "pushfps-target-row").visible, "and nothing else of the quick setup")
            verify(findChild(page, "list-nr-stage") !== null && findChild(page, "list-sr") !== null)
            page.tab = "fg"
            tryVerify(function() { return rest.visible && !picture.visible }, 3000)
            verify(control(rest, "pushfps-tools-row").visible && control(rest, "pushfps-target-row").visible, "the Frame Gen tab keeps the rest")
            verify(!control(rest, "pushfps-section-picture").visible && !control(rest, "pushfps-crop-row").visible && !control(rest, "pushfps-fine-row").visible,
                   "and no longer the picture's settings")
        }
    }
}
