// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// Appearance modes (Theme.mode: default, black, light): every text level and state
// colour must stay readable on every surface it is drawn on (WCAG contrast ratios),
// and overlays (Theme.ink) must turn dark on light surfaces.
import QtQuick
import QtTest
import Veyra
Item {
    id: root
    width: 200; height: 200

    TestCase {
        name: "PushAppearance"
        readonly property var modes: ["default", "black", "light", "midnight", "plum", "forest", "contrast"]
        readonly property var accents: ["orange", "white", "ice", "blue", "violet", "rose", "amber"]
        readonly property var surfaces: ["card", "card2", "card3", "popover", "dialog", "bgInner"]
        function cleanup() { Theme.mode = "default"; Theme.accentName = "orange"; Theme.design = "legacy" }

        function lin(c) { return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4) }
        function luminance(r, g, b) { return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b) }
        // Contrast of colour fg (alpha composited) drawn on the opaque colour bg.
        function contrast(fg, bg) {
            const a = fg.a
            const r = fg.r * a + bg.r * (1 - a), g = fg.g * a + bg.g * (1 - a), b = fg.b * a + bg.b * (1 - a)
            const l1 = luminance(r, g, b), l2 = luminance(bg.r, bg.g, bg.b)
            return (Math.max(l1, l2) + 0.05) / (Math.min(l1, l2) + 0.05)
        }

        function test_text_is_readable_in_every_mode() {
            for (const m of modes) {
                Theme.mode = m
                for (const s of surfaces) {
                    const bg = Theme[s]
                    verify(contrast(Theme.t1, bg) >= 7, m + " t1 on " + s)
                    verify(contrast(Theme.tLabel, bg) >= 7, m + " tLabel on " + s)
                    verify(contrast(Theme.t2, bg) >= 4.5, m + " t2 on " + s + ": " + contrast(Theme.t2, bg).toFixed(2))
                    verify(contrast(Theme.t3, bg) >= 4.0, m + " t3 on " + s + ": " + contrast(Theme.t3, bg).toFixed(2))
                }
            }
        }
        function test_accent_and_states_are_readable() {
            for (const m of modes) {
                Theme.mode = m
                for (const name of accents) {
                    Theme.accentName = name
                    for (const s of ["card", "card2", "card3"]) {
                        verify(contrast(Theme.accent, Theme[s]) >= 3, m + "/" + name + " accent on " + s)
                    }
                    verify(contrast(Theme.accentInk, Theme.accent) >= 4.5, m + "/" + name + " text on accent")
                }
                for (const c of ["ok", "warn", "err", "exp"])
                    verify(contrast(Theme[c], Theme.card2) >= 3, m + " " + c + " on card2: " + contrast(Theme[c], Theme.card2).toFixed(2))
            }
        }
        // The redesign: the new design's own graphite and light surfaces meet the same demands, with every
        // accent (the other themes are one table for both designs, so the tests above already hold them).
        function test_new_design_is_as_readable() {
            Theme.design = "new"
            for (const m of ["default", "light"]) {
                Theme.mode = m
                verify(Theme.modernTone, m)
                for (const s of surfaces) {
                    const bg = Theme[s]
                    verify(contrast(Theme.t1, bg) >= 7, m + " t1 on " + s)
                    verify(contrast(Theme.tLabel, bg) >= 7, m + " tLabel on " + s)
                    verify(contrast(Theme.t2, bg) >= 4.5, m + " t2 on " + s + ": " + contrast(Theme.t2, bg).toFixed(2))
                    verify(contrast(Theme.t3, bg) >= 4.5, m + " t3 on " + s + ": " + contrast(Theme.t3, bg).toFixed(2))
                }
                for (const name of accents) {
                    Theme.accentName = name
                    for (const s of ["card", "card2", "card3"])
                        verify(contrast(Theme.accent, Theme[s]) >= 3, m + "/" + name + " accent on " + s)
                    verify(contrast(Theme.accentInk, Theme.accent) >= 4.5, m + "/" + name + " text on accent")
                }
                Theme.accentName = "orange"
                for (const c of ["ok", "warn", "err", "exp"])
                    verify(contrast(Theme[c], Theme.card2) >= 3, m + " " + c + " on card2: " + contrast(Theme[c], Theme.card2).toFixed(2))
            }
            // The page's own background is a surface text stands on too (the status strip, the panes' gaps).
            Theme.mode = "default"
            verify(contrast(Theme.t3, Theme.bgOuter) >= 4.5 && contrast(Theme.t2, Theme.bgOuter) >= 7)
            // The tiles of the theme pickers show each theme as this design draws it.
            compare(String(Theme.surfaceOf("default").card2).toLowerCase(), "#1c242e")
            compare(String(Theme.surfaceOf("midnight").card2).toLowerCase(), "#141b2b")
            compare(String(Theme.surfaceOf("nonsense").card2).toLowerCase(), "#19191e", "an unknown name is the dark grey, never a broken colour")
            Theme.design = "legacy"
            compare(String(Theme.surfaceOf("default").card2).toLowerCase(), "#19191e")
        }
        function test_ink_follows_the_mode() {
            Theme.mode = "default"
            verify(Theme.ink(0.5).r > 0.99, "white overlay on dark surfaces")
            Theme.mode = "black"
            verify(Theme.ink(0.5).r > 0.99)
            verify(Theme.card.r < 0.05 && Theme.bgOuter.r === 0, "black mode is almost black")
            Theme.mode = "light"
            verify(Theme.ink(0.5).r < 0.1, "dark overlay on light surfaces")
            verify(Theme.light && !Theme.black)
            verify(Theme.card.r > 0.95, "light surfaces")
            compare(String(Theme.onVideo), "#ffffff", "text over the picture stays white")
        }
        // High contrast: every level of text far above what the other looks ask for, and an outline that
        // shows where a card ends on a background of the same black.
        function test_high_contrast() {
            Theme.mode = "contrast"
            verify(Theme.contrast && !Theme.light && !Theme.black && !Theme.tinted)
            for (const s of surfaces) {
                const bg = Theme[s]
                verify(contrast(Theme.t1, bg) >= 15, "t1 on " + s)
                verify(contrast(Theme.t2, bg) >= 12, "t2 on " + s + ": " + contrast(Theme.t2, bg).toFixed(2))
                verify(contrast(Theme.t3, bg) >= 10, "t3 on " + s + ": " + contrast(Theme.t3, bg).toFixed(2))
            }
            verify(contrast(Theme.stroke, Theme.card) >= 4.5, "a card's outline: " + contrast(Theme.stroke, Theme.card).toFixed(2))
            verify(contrast(Theme.stroke2, Theme.card) >= 7, "a control's outline")
            verify(Theme.ink(0.1).a > 0.17, "overlays (tracks, hover) are stronger")
            Theme.mode = "default"
            verify(!Theme.contrast)
            compare(String(Theme.t1), "#f3f3f5", "and the other looks are what they were")
            verify(Math.abs(Theme.stroke.a - 0.07) < 0.005)
        }
        // The tinted backgrounds are dark ones: white overlays, their own surfaces, and the dark grey's values untouched.
        function test_tinted_backgrounds_and_more_accents() {
            Theme.mode = "default"
            compare(String(Theme.card2), "#19191e"); compare(String(Theme.bgOuter), "#070709")
            compare(String(Theme.accent), "#89f52a", "the default accent is the brand green")
            const seen = {}
            for (const m of ["midnight", "plum", "forest"]) {
                Theme.mode = m
                verify(Theme.tinted && !Theme.light && !Theme.black, m)
                verify(Theme.ink(0.5).r > 0.99, m + ": white overlay on a dark surface")
                verify(Theme.card2.r < 0.2 && Theme.card2.g < 0.2 && Theme.card2.b < 0.2, m + " is dark")
                verify(!seen[String(Theme.card2)], m + " has its own surfaces"); seen[String(Theme.card2)] = true
            }
            // An unknown mode or accent is the default, never a broken colour.
            Theme.mode = "nonsense"; compare(String(Theme.card2), "#19191e")
            Theme.accentName = "nonsense"; compare(String(Theme.accent), "#89f52a")
            // Every accent is its own colour, and the white one keeps its quiet soft fill.
            Theme.mode = "default"
            const colours = {}
            for (const a of accents) { Theme.accentName = a; verify(!colours[String(Theme.accent)], a); colours[String(Theme.accent)] = true }
            Theme.accentName = "ice"
            verify(Math.abs(Theme.accentSoft.a - 0.14) < 0.01 && Math.abs(Theme.accentSoft.r - Theme.accent.r) < 0.01, "the soft fill is the accent, thinned")
        }
    }
}
