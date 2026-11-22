// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The redesign (after 4.2.1): the new design's shell (navigation by name, title bar, status strip), the
// home page as three panes (sources | the picture | quick setup), the Appearance panel behind the
// painter's palette, and the rule the whole thing stands on: nothing is lost, and Legacy is the
// interface as it was. The main window itself is in no test (it needs the program); its parts are.
import QtQuick
import QtTest
import Veyra
import VeyraTest 1.0   // the home page is private to the shell's folder; the test program registers it here

Item {
    id: fixture
    width: 1600; height: 900

    PushSidebar { id: nav; x: 0; y: 0; width: 208; height: 600; z: 5; visible: false }
    PushTitleBar { id: bar; x: 0; y: 0; width: 1000; height: 46; z: 5; visible: false; pageName: "Home" }
    PushStatusStrip { id: strip; x: 0; y: 60; width: 1000; height: 34; z: 5; visible: false }
    // A page takes its parent, size, visibility and input from the shell (VPage.qml): outside the shell a
    // test sets all five. The size is the page's inside a 1600 x 900 window of the new design.
    HomePage { id: home; parent: fixture; width: 1392; height: 820; visible: false; enabled: true }
    Item { id: host; anchors.fill: parent; property string dialog: ""; property real motionScale: 1; function close() { dialog = "" } }
    PushAppearanceDialog { id: look; dialogHost: host }

    SignalSpy { id: pages; target: nav; signalName: "requestPage" }
    SignalSpy { id: palettes; target: nav; signalName: "requestAppearance" }
    SignalSpy { id: minimizes; target: bar; signalName: "requestMinimize" }
    SignalSpy { id: maximizes; target: bar; signalName: "requestMaximize" }
    SignalSpy { id: closes; target: bar; signalName: "requestClose" }
    SignalSpy { id: navToggles; target: bar; signalName: "requestToggleNav" }
    SignalSpy { id: sideToggles; target: bar; signalName: "requestToggleSide" }
    SignalSpy { id: fullscreens; target: home; signalName: "requestFullscreen" }
    SignalSpy { id: homePalettes; target: home; signalName: "requestAppearance" }
    SignalSpy { id: moreSettings; target: look; signalName: "requestSettings" }

    TestCase {
        name: "PushDesign"
        when: windowShown
        property var savedPreferences: ({})
        property bool savedSource: false
        function init() {
            savedPreferences = veyra.preferences; savedSource = veyra.hasSource
            veyra.preferences = ({})
            veyra.hasSource = false
            veyra.liveStats = ({})
            Theme.design = "new"; Theme.mode = "default"; Theme.accentName = "orange"
            nav.visible = false; bar.visible = false; strip.visible = false; home.visible = false
            nav.slim = false; nav.currentPage = "home"
            home.width = 1392; home.height = 820
            host.dialog = ""
            bar.navToggleShown = false; bar.sideToggleShown = false; bar.navCollapsed = false; bar.sideCollapsed = false
            for (const spy of [pages, palettes, minimizes, maximizes, closes, navToggles, sideToggles, fullscreens, homePalettes, moreSettings]) spy.clear()
        }
        function cleanup() {
            host.dialog = ""
            nav.visible = false; bar.visible = false; strip.visible = false; home.visible = false
            Theme.design = "legacy"; Theme.mode = "default"; Theme.accentName = "orange"
            veyra.preferences = savedPreferences; veyra.hasSource = savedSource
        }
        function control(parent, name) { const c = findChild(parent, name); verify(c !== null, name); return c }
        // Whether an item is drawn: itself and everything it stands in visible, and of a size.
        function drawn(item) { return item !== null && item.visible && item.width > 0 && item.height > 0 }

        // The design is a choice, and without the main window (which reads the preference) it is Legacy:
        // every other test in this folder sees the interface it always saw.
        function test_legacy_is_the_interface_as_it_was() {
            Theme.design = "legacy"
            verify(!Theme.modern && !Theme.modernTone)
            compare(String(Theme.card2), "#19191e"); compare(String(Theme.bgOuter), "#070709"); compare(String(Theme.t1), "#f3f3f5")
            compare(Theme.rCard, 14); compare(Theme.rCtl, 10)
            Theme.mode = "light"
            compare(String(Theme.card2), "#f3f5f0")
            // The new design has its own graphite and its own light surfaces...
            Theme.design = "new"; Theme.mode = "default"
            verify(Theme.modern && Theme.modernTone)
            compare(String(Theme.card), "#141a21"); compare(String(Theme.card2), "#1c242e"); compare(String(Theme.bgOuter), "#0b0f14")
            compare(String(Theme.t1), "#f4f7fa"); compare(String(Theme.accent), "#89f52a", "the brand green in both")
            Theme.mode = "light"
            compare(String(Theme.card), "#ffffff"); compare(String(Theme.bgOuter), "#f5f7fa"); compare(String(Theme.accent), "#2f7a06")
            // ...and every other theme is one table for both designs.
            for (const m of ["black", "midnight", "plum", "forest", "contrast"]) {
                Theme.design = "new"; Theme.mode = m
                const inNew = String(Theme.card2) + String(Theme.t2) + String(Theme.stroke)
                verify(!Theme.modernTone, m)
                Theme.design = "legacy"
                compare(String(Theme.card2) + String(Theme.t2) + String(Theme.stroke), inNew, m)
            }
        }

        // Every mode by name, always on screen; the palette at the bottom.
        function test_navigation_lists_every_mode_by_name() {
            nav.visible = true
            const ids = ["home", "pro", "live", "watch", "exp", "min", "set"]
            compare(nav.entries.length, ids.length)
            for (let i = 0; i < ids.length; ++i) {
                compare(nav.entries[i].id, ids[i])
                verify(nav.entries[i].label.length > 0, ids[i] + " has a name")
                const entry = control(nav, "pushfps-nav-" + ids[i])
                tryVerify(function() { return drawn(entry) && entry.width > 100 }, 3000, ids[i] + " is on screen")
                mouseClick(entry)
                compare(pages.count, i + 1)
                compare(pages.signalArguments[i][0], ids[i], "an entry asks for the page the dock asked for")
            }
            // No two entries share a place.
            for (let i = 1; i < ids.length; ++i) {
                const above = control(nav, "pushfps-nav-" + ids[i - 1]), below = control(nav, "pushfps-nav-" + ids[i])
                verify(below.mapToItem(nav, 0, 0).y >= above.mapToItem(nav, 0, 0).y + above.height, ids[i] + " stands under " + ids[i - 1])
            }
            nav.currentPage = "live"
            verify(control(nav, "pushfps-nav-live").current && !control(nav, "pushfps-nav-home").current)
            compare(palettes.count, 0)
            const palette = control(nav, "pushfps-nav-appearance")
            verify(palette.mapToItem(nav, 0, 0).y > control(nav, "pushfps-nav-set").mapToItem(nav, 0, 0).y + 100, "the palette stands at the bottom")
            mouseClick(palette)
            compare(palettes.count, 1); compare(pages.count, ids.length, "and is not a page")
            // A narrow window: the icons alone, every entry still there.
            nav.width = 60; nav.slim = true
            for (const id of ids) tryVerify(function() { return drawn(control(nav, "pushfps-nav-" + id)) }, 3000, id)
            mouseClick(control(nav, "pushfps-nav-pro"))
            compare(pages.signalArguments[pages.count - 1][0], "pro")
            nav.width = 208
        }

        function test_title_bar_has_the_window_buttons() {
            bar.visible = true
            const min = control(bar, "pushfps-window-min"), max = control(bar, "pushfps-window-max"), close = control(bar, "pushfps-window-close")
            tryVerify(function() { return drawn(min) && drawn(max) && drawn(close) }, 3000)
            verify(min.mapToItem(bar, 0, 0).x + min.width <= max.mapToItem(bar, 0, 0).x + 0.5, "side by side, not over each other")
            verify(max.mapToItem(bar, 0, 0).x + max.width <= close.mapToItem(bar, 0, 0).x + 0.5)
            verify(close.mapToItem(bar, 0, 0).x + close.width <= bar.width)
            compare(control(bar, "pushfps-titlebar-page").text, "Home")
            mouseClick(min); compare(minimizes.count, 1)
            mouseClick(close); compare(closes.count, 1)
            // Two quick presses on a button are that button twice, never a double click on the bar.
            mouseClick(max); mouseClick(max)
            compare(maximizes.count, 2)
            compare(minimizes.count, 1); compare(closes.count, 1)
            // The part of the bar that moves the window (and maximises it on a double click) ends before the buttons.
            const drag = control(bar, "pushfps-titlebar-drag")
            verify(drag.width > 500 && drag.x + drag.width <= min.mapToItem(bar, 0, 0).x + 0.5, "the bar's own handlers never see a press on a button")
        }

        // 4.3.1: the two side buttons. One at the bar's left end folds the navigation to its icons, one at
        // its right end hides the settings side; each is there only where it has something to fold.
        function test_side_buttons_stand_at_their_ends_and_ask_for_their_side() {
            bar.visible = true
            const navButton = control(bar, "pushfps-nav-toggle"), sideButton = control(bar, "pushfps-side-toggle")
            const min = control(bar, "pushfps-window-min"), drag = control(bar, "pushfps-titlebar-drag")
            verify(!navButton.visible && !sideButton.visible, "not shown where there is nothing to fold")
            const plain = drag.width
            bar.navToggleShown = true; bar.sideToggleShown = true
            tryVerify(function() { return drawn(navButton) && drawn(sideButton) }, 3000)
            const left = item => item.mapToItem(bar, 0, 0).x
            // Each at its own end, beside what is there and over nothing.
            tryVerify(function() { return left(navButton) >= 0 && left(navButton) + navButton.width <= drag.x + 0.5 }, 3000,
                      "the navigation's button stands left of the part that moves the window")
            tryVerify(function() { return left(sideButton) >= drag.x + drag.width - 0.5 && left(sideButton) + sideButton.width <= left(min) + 0.5 }, 3000,
                      "the settings side's button stands between that part and the window's buttons")
            verify(left(navButton) + navButton.width < bar.width / 2 && left(sideButton) > bar.width / 2, "one left, one right")
            verify(drag.width > 400 && drag.width < plain, "the bar is still moved by what is between them")
            // Each asks for its own side; two quick presses are that button twice, never a double click on the bar.
            mouseClick(navButton)
            compare(navToggles.count, 1); compare(sideToggles.count, 0)
            mouseClick(sideButton); mouseClick(sideButton)
            compare(sideToggles.count, 2); compare(navToggles.count, 1)
            compare(maximizes.count, 0); compare(minimizes.count, 0); compare(closes.count, 0)
            // What a button says follows what it would do.
            verify(navButton.shown && sideButton.shown)
            const navTip = navButton.tip, sideTip = sideButton.tip
            verify(navTip.length > 0 && sideTip.length > 0 && navTip !== sideTip)
            bar.navCollapsed = true; bar.sideCollapsed = true
            verify(!navButton.shown && !sideButton.shown)
            verify(navButton.tip !== navTip && sideButton.tip !== sideTip && navButton.tip.length > 0 && sideButton.tip.length > 0)
            // Without them the bar is what it was.
            bar.navToggleShown = false; bar.sideToggleShown = false
            tryVerify(function() { return !navButton.visible && !sideButton.visible && Math.abs(drag.width - plain) < 0.5 }, 3000)
        }

        // 4.3.1: the home page's settings side hidden; the picture takes its room, and whatever asks for a
        // part of quick setup brings the side back.
        function test_home_hides_its_settings_side_and_the_picture_takes_the_room() {
            home.visible = true
            tryVerify(function() { return home.threePane }, 3000)
            const picture = control(home, "home-desk-preview"), settings = control(home, "home-desk-settings"), sources = control(home, "home-desk-sources")
            tryVerify(function() { return drawn(settings) && picture.width > 300 }, 3000)
            const before = picture.width, viewBefore = home.videoViewport.width
            verify(!home.sideCollapsed)
            veyra.setPreference("pushSideCollapsed", true)
            tryVerify(function() { return home.sideCollapsed && !settings.visible }, 3000, "quick setup's pane is not drawn")
            tryVerify(function() { return picture.width > before + 300 }, 3000, "the picture's column takes the room: " + picture.width)
            verify(picture.x + picture.width <= home.width - home.paneMargin + 0.5, "and stays inside the page")
            const view = home.videoViewport
            verify(view.width > viewBefore && view.x >= picture.x && view.x + view.width <= picture.x + picture.width, "the picture's place follows")
            verify(sources.x + sources.width <= picture.x, "the sources stay where they are")
            tryVerify(function() { return drawn(control(home, "pushfps-optimize")) }, 3000, "the tools stay under the picture")
            // Asking for a part of quick setup shows the side again.
            home.showInQuick(home.quickIndex[2])
            tryVerify(function() { return !home.sideCollapsed && drawn(settings) }, 3000, "the index brings it back")
            compare(veyra.preferences.pushSideCollapsed, false)
            tryVerify(function() { return Math.abs(picture.width - before) < 1 }, 3000)
            veyra.setPreference("pushSideCollapsed", true)
            tryVerify(function() { return home.sideCollapsed }, 3000)
            home.showTourStep("target")
            tryVerify(function() { return !home.sideCollapsed }, 3000, "so does the tour")
            // The one column has no side to hide.
            veyra.setPreference("pushSideCollapsed", true)
            Theme.design = "legacy"
            tryVerify(function() { return !home.threePane }, 3000)
            verify(!home.sideCollapsed)
            tryVerify(function() { return drawn(control(home, "home-pushfps-quick")) }, 3000, "Legacy: quick setup is on the page, whatever was saved")
        }

        // A figure nobody reported is a dash: never a zero, never a made-up number.
        function test_status_strip_never_invents_a_figure() {
            strip.visible = true
            const cell = name => control(strip, "pushfps-status-" + name)
            veyra.hasSource = false
            strip.refresh()
            for (const name of ["capture", "rate", "gpu", "output"]) { compare(cell(name).value, "—", name + " with nothing playing"); verify(!cell(name).known) }
            verify(cell("latency").value.indexOf("—") >= 0 && !cell("latency").known)
            // Playing, and the engine has said nothing yet: still dashes (Number(null) is 0, and 0 is not a reading).
            veyra.hasSource = true
            veyra.liveStats = ({ running: true, captureFps: null, contentFps: "", latencyMs: null, gpuUtilization: undefined })
            strip.refresh()
            for (const name of ["capture", "rate", "gpu", "output"]) compare(cell(name).value, "—", name + " before the first reading")
            verify(cell("latency").value.indexOf("—") >= 0 && cell("latency").value.indexOf("0") < 0)
            // The engine's own numbers, as reported.
            veyra.liveStats = ({ running: true, sourceWidth: 3840, sourceHeight: 2160, captureFps: 59.94, contentFps: 30, realFps: 30, generatedFps: 90,
                                 latencyMs: 41.6, gpuUtilization: 23.4, outputWidth: 3840, outputHeight: 2160, hdr: true })
            strip.refresh()
            compare(cell("capture").value, "3840 × 2160 · 60 FPS")
            compare(cell("rate").value, "30 → 120 FPS")
            verify(cell("latency").value.indexOf("42") >= 0 && cell("latency").known)
            compare(cell("gpu").value, "23 %")
            compare(cell("output").value, "3840 × 2160 · HDR")
            // A size without a rate is the size, not a size and a dash.
            veyra.liveStats = ({ running: true, sourceWidth: 1280, sourceHeight: 720 })
            strip.refresh()
            compare(cell("capture").value, "1280 × 720")
            // Stopped: the last figures do not stay on screen.
            veyra.hasSource = false
            tryCompare(cell("capture"), "value", "—")
            compare(cell("rate").value, "—")
        }

        // The home page of the new design: sources left, the picture in the middle, quick setup right.
        function test_home_is_three_panes_and_nothing_is_lost() {
            home.visible = true
            tryVerify(function() { return home.threePane }, 3000)
            const desk = control(home, "home-desk")
            tryVerify(function() { return drawn(desk) }, 3000)
            const sources = control(home, "home-desk-sources"), picture = control(home, "home-desk-preview"), settings = control(home, "home-desk-settings")
            tryVerify(function() { return sources.width > 200 && picture.width > 300 && settings.width > 300 }, 3000)
            // Three panes beside each other, inside the page, none over another.
            verify(sources.x >= 0 && sources.x + sources.width <= picture.x, "sources left of the picture")
            verify(picture.x + picture.width <= settings.x, "quick setup right of it")
            verify(settings.x + settings.width <= home.width)
            // The picture's place is 16:9 and is where the native picture window is put.
            const view = home.videoViewport
            verify(view.width > 300 && Math.abs(view.width / view.height - 16 / 9) < 0.03, "16:9: " + view.width + " x " + view.height)
            verify(view.x >= picture.x && view.x + view.width <= picture.x + picture.width && view.y >= picture.y && view.y + view.height <= picture.y + picture.height)
            // Everything the home page had is still on it, and drawn.
            const names = ["home-capture", "home-pushfps-quick", "home-picture-sound", "home-onboarding",
                           "pushfps-optimize", "pushfps-diagnose", "pushfps-game-presets", "pushfps-compare-clip", "pushfps-stream",
                           "pushfps-section-setup", "pushfps-section-fg", "pushfps-section-picture", "pushfps-enhance-row", "pushfps-upscale-row", "pushfps-crop-row", "pushfps-section-latency"]
            for (const name of names) {
                const item = control(home, name)
                tryVerify(function() { return drawn(item) }, 3000, name + " is on the new home page")
            }
            // The whole of quick setup: the five rows the short form leaves to Professional mode are here too.
            const whole = ["pushfps-smooth-row", "pushfps-low-latency-row", "pushfps-auto-connect-row", "pushfps-auto-fullscreen-row", "pushfps-section-start"]
            for (const name of whole) {
                const item = control(home, name)
                tryVerify(function() { return drawn(item) }, 3000, name + " is on the new home page")
            }
            // The tools are the same tiles, drawn under the picture; the first-run tip stands in the empty picture.
            const tools = control(home, "pushfps-tools-row"), place = control(home, "home-desk-tools")
            compare(tools.parent, place)
            tryVerify(function() { return place.height > 40 && Math.abs(tools.width - place.width) < 1 }, 3000)
            verify(!control(home, "pushfps-setup-tip").visible)
            veyra.hasCaptureSession = false
            tryVerify(function() { return control(home, "home-desk-tip").visible }, 3000)
            verify(control(home, "home-desk-tip").text.length > 20)
            // Legacy: the one column, every part of it, and no picture on the page.
            Theme.design = "legacy"
            tryVerify(function() { return !home.threePane && !desk.visible }, 3000)
            compare(home.videoViewport.width, 0)
            verify(tools.parent !== place, "the tools are back in the quick setup card")
            for (const name of names.concat(["pushfps-setup-tip", "home-capture-connect", "home-credit"])) {
                const item = control(home, name)
                tryVerify(function() { return drawn(item) }, 3000, name + " is on the legacy home page")
            }
            // Legacy's home page has the short form of quick setup, as it always had.
            for (const name of ["pushfps-smooth-row", "pushfps-low-latency-row", "pushfps-auto-connect-row", "pushfps-auto-fullscreen-row"])
                verify(!control(home, name).visible, name + " is Professional mode's in Legacy")
            // Its palette: the way to the Appearance panel, and so to the new design.
            const palette = control(home, "home-appearance")
            tryVerify(function() { return drawn(palette) }, 3000)
            mouseClick(palette)
            compare(homePalettes.count, 1)
            // The new design in a window too narrow for three panes: the one column inside the shell.
            Theme.design = "new"
            tryVerify(function() { return home.threePane }, 3000)
            verify(!palette.visible, "the new design has the palette in its navigation")
            home.width = 800
            tryVerify(function() { return !home.threePane && !desk.visible }, 3000)
            tryVerify(function() { return drawn(control(home, "home-capture-connect")) }, 3000)
            compare(home.videoViewport.width, 0)
        }

        function test_the_empty_picture_offers_the_ways_in() {
            home.visible = true
            tryVerify(function() { return home.threePane && drawn(control(home, "home-desk-empty")) }, 3000)
            const before = veyra.captureDialogs, files = veyra.fileDialogs
            mouseClick(control(home, "home-desk-connect"))
            compare(veyra.captureDialogs, before + 1, "the capture dialog, as the capture card's own button opens it")
            mouseClick(control(home, "home-desk-open"))
            compare(veyra.fileDialogs, files + 1)
            const big = control(home, "home-desk-fullscreen")
            verify(!big.enabled, "nothing to show in full screen yet")
            verify(!control(home, "home-desk-stop").visible)
            // Playing: the picture is the native window's, and the two actions under it work.
            veyra.hasSource = true
            tryVerify(function() { return !control(home, "home-desk-empty").visible && big.enabled }, 3000)
            mouseClick(big)
            compare(fullscreens.count, 1)
            const stops = veyra.playbackStops
            const stop = control(home, "home-desk-stop")
            tryVerify(function() { return drawn(stop) }, 3000)
            mouseClick(stop)
            compare(veyra.playbackStops, stops + 1)
        }

        // Nothing is hidden: every section of quick setup, and AI picture enhancement by its own name, is
        // one press away at the top of the list.
        function test_index_brings_a_section_to_the_top() {
            home.visible = true
            tryVerify(function() { return home.threePane && drawn(control(home, "home-desk-index")) }, 3000)
            const list = control(home, "home-desk-settings-list")
            tryVerify(function() { return list.contentHeight > list.height + 200 }, 3000, "the list is longer than the pane")
            compare(list.contentY, 0)
            for (const id of ["setup", "fg", "enhance", "picture", "latency", "start"]) {
                const chip = control(home, "home-desk-index-" + id)
                tryVerify(function() { return drawn(chip) }, 3000, id + " is in the index")
                verify(chip.mapToItem(home, 0, 0).x + chip.width <= home.width, id + " is inside the pane")
            }
            // AI picture enhancement: below the fold until asked for, then in view.
            const row = control(home, "pushfps-enhance-row")
            const atEnd = () => list.contentY >= list.contentHeight - list.height - 1
            verify(row.mapToItem(list, 0, 0).y > 150, "it starts down the list: " + row.mapToItem(list, 0, 0).y)
            mouseClick(control(home, "home-desk-index-enhance"))
            tryVerify(function() { const y = row.mapToItem(list, 0, 0).y; return y >= 0 && y + row.height <= list.height && (y <= 12 || atEnd()) }, 3000,
                      "the enhancement's switch is brought to the top of the list")
            // A folded section is opened, then shown.
            veyra.setPreference("pushQuickFolded", "latency")
            const head = control(home, "pushfps-section-latency")
            mouseClick(control(home, "home-desk-index-latency"))
            tryVerify(function() { return String(veyra.preferences.pushQuickFolded || "").indexOf("latency") < 0 }, 3000, "opened")
            tryVerify(function() { const y = head.mapToItem(list, 0, 0).y; return y >= -1 && (y <= 12 || atEnd()) && y < list.height - 20 }, 3000, "and shown")
            // Back to the top.
            mouseClick(control(home, "home-desk-index-setup"))
            tryCompare(list, "contentY", 0)
        }

        // The panel behind the palette: the design, the theme, the accent. Each is the preference Settings writes.
        function test_appearance_panel_switches_the_design_and_the_colours() {
            host.dialog = "appearance"
            verify(look.shown)
            const design = control(look, "pushfps-appearance-design")
            tryVerify(function() { return drawn(design) }, 3000)
            compare(design.current, "new")
            design.picked("legacy")
            compare(veyra.preferences.pushDesign, "legacy")
            design.picked("new")
            compare(veyra.preferences.pushDesign, "new")
            Theme.design = "legacy"
            compare(design.current, "legacy", "it shows the design on screen")
            tryVerify(function() { return drawn(control(look, "pushfps-appearance-sunset")) }, 3000, "the Sunset City logo is the legacy home page's")
            Theme.design = "new"
            // Seven themes in one row, none over another, all inside the dialog.
            const themes = ["default", "light", "black", "midnight", "plum", "forest", "contrast"]
            let right = -1
            for (const id of themes) {
                const tile = control(look, "pushfps-appearance-theme-" + id)
                tryVerify(function() { return drawn(tile) }, 3000, id)
                const x = tile.mapToItem(look, 0, 0).x
                verify(x >= right, id + " stands beside the one before it")
                right = x + tile.width
            }
            const row = control(look, "pushfps-appearance-theme-row")
            verify(right <= row.mapToItem(look, 0, 0).x + row.width + 0.5, "the row holds its tiles")
            mouseClick(control(look, "pushfps-appearance-theme-light"))
            compare(veyra.preferences.pushTheme, "light")
            mouseClick(control(look, "pushfps-appearance-theme-midnight"))
            compare(veyra.preferences.pushTheme, "midnight")
            mouseClick(control(look, "pushfps-appearance-accent-ice"))
            compare(veyra.preferences.accent, "ice")
            control(look, "pushfps-appearance-intro").toggled(false)
            compare(veyra.preferences.introAnimation, false)
            mouseClick(control(look, "pushfps-appearance-more"))
            compare(moreSettings.count, 1)
        }
    }
}
