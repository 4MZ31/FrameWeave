// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// The crash notice (PushCrashDialog), the graphics card test (PushCardTestDialog) and what its
// timings mean (PushCardTest.js), against the fixture bridge.
import QtQuick
import QtTest
import Veyra
import "../qml/Veyra/PushCardTest.js" as CardTest
Item {
    id: root
    width: 900; height: 900
    Item { id: host; anchors.fill: parent; property string dialog: ""; function close() { dialog = "" } }
    PushCrashDialog { id: crash; dialogHost: host }
    PushCardTestDialog { id: card; dialogHost: host }

    // A benchmark result as PushFlowBench writes it: one run per size and tier.
    function run(w, h, tier, ms2, ms3, ms4) {
        return { width: w, height: h, tier: tier, precision: "fp32", networkMs: { p50: ms2 * 0.8, p95: ms2 * 0.9 },
                 pairWallMs: { "2x": { p50: ms2 * 0.9, p95: ms2 }, "3x": { p50: ms3 * 0.9, p95: ms3 }, "4x": { p50: ms4 * 0.9, p95: ms4 } } }
    }
    readonly property var fastCard: [run(1920, 1080, "Fast", 2, 4, 6), run(1920, 1080, "Balanced", 3, 6, 9), run(1920, 1080, "Quality", 6, 12, 18),
                                     run(3840, 2160, "Fast", 3, 6, 9), run(3840, 2160, "Balanced", 4, 8, 12), run(3840, 2160, "Quality", 8, 16, 24)]
    readonly property var slowCard: [run(1920, 1080, "Fast", 9, 18, 27), run(1920, 1080, "Balanced", 15, 30, 45), run(1920, 1080, "Quality", 40, 80, 120),
                                     run(3840, 2160, "Fast", 12, 24, 36), run(3840, 2160, "Balanced", 20, 40, 60), run(3840, 2160, "Quality", 50, 100, 150)]

    TestCase {
        name: "PushTools"
        when: windowShown
        function init() {
            host.dialog = ""
            veyra.crashReport = ({present: false}); veyra.crashDismissed = 0; veyra.logFolderOpened = 0
            veyra.lastCopiedText = ""; veyra.lastOpenedLink = ""
            veyra.cardTestState = ({running: false, done: false}); veyra.cardTestNext = ({running: true, done: false})
            veyra.cardTestStarts = 0; veyra.cardTestCancels = 0
        }
        function cleanup() { init() }
        function control(item, name) { const c = findChild(item, name); verify(c !== null, name); return c }
        function labels(dialog) { return dialog.actions.map(a => a.label) }

        // --- crash notice -----------------------------------------------------
        function test_crash_notice_shows_the_summary_and_copies_it() {
            const summary = "PushFPS crash summary\nVersion: 3.2.2\nCrashed in: onnxruntime.dll +0x11B640 (code 0xC0000005)\nPlaying: xbox 1920x1080\n"
            veyra.crashReport = ({present: true, module: "onnxruntime.dll", dump: "pushfps-crash-20261118-211405-1234.dmp", optimising: true, summary: summary})
            host.dialog = "crash"
            verify(crash.shown && crash.present)
            compare(control(crash, "pushfps-crash-summary").text, summary)
            // The dialog fades in, and a row inside it only reads visible once the dialog does.
            tryVerify(function() { return control(crash, "pushfps-crash-file-row").visible })
            verify(control(crash, "pushfps-crash-file-row").hint.indexOf("pushfps-crash-20261118-211405-1234.dmp") >= 0, "the crash file is named")
            compare(crash.actions.length, 3)
            crash.actionTriggered(labels(crash)[2])
            compare(veyra.lastCopiedText, summary, "the primary button copies the summary")
            verify(crash.copied)
            control(crash, "pushfps-crash-folder").clicked()
            compare(veyra.logFolderOpened, 1)
            veyra.lastCopiedText = ""
            crash.actionTriggered(labels(crash)[1])
            compare(veyra.lastOpenedLink, "issues", "feedback opens the issue tracker")
            compare(veyra.lastCopiedText, summary, "with the summary on the clipboard")
            compare(veyra.crashDismissed, 0, "still open: not forgotten yet")
            crash.actionTriggered(labels(crash)[0])
            compare(host.dialog, "")
            compare(veyra.crashDismissed, 1, "closed: it does not come back")
        }
        function test_crash_notice_without_a_crash_says_so() {
            host.dialog = "crash"
            verify(crash.shown && !crash.present)
            tryVerify(function() { return crash.visible })
            verify(!control(crash, "pushfps-crash-file-row").visible, "nothing about a crash file without a crash")
            compare(crash.actions.length, 1)
            crash.actionTriggered(labels(crash)[0])
            compare(host.dialog, "")
            compare(veyra.crashDismissed, 0)
            compare(veyra.lastCopiedText, "")
        }
        function test_crash_notice_from_an_older_note_has_no_file_name() {
            veyra.crashReport = ({present: true, module: "", dump: "", optimising: false, summary: "PushFPS crash summary\n"})
            host.dialog = "crash"
            verify(control(crash, "pushfps-crash-file-row").hint.indexOf(".dmp") >= 0, "says which kind of file to look for")
            host.dialog = ""
            compare(veyra.crashDismissed, 1, "closing any way forgets it")
        }

        // --- what the timings mean -----------------------------------------------
        function test_card_rules() {
            // A 30 FPS game leaves 33.3 ms per pair, 80 % of it is 26.7 ms; a 60 FPS game 13.3 ms.
            verify(CardTest.fits(root.run(1920, 1080, "Fast", 26, 30, 40), 30, 2))
            verify(!CardTest.fits(root.run(1920, 1080, "Fast", 27, 30, 40), 30, 2))
            verify(CardTest.fits(root.run(1920, 1080, "Fast", 13, 30, 40), 60, 2))
            verify(!CardTest.fits(root.run(1920, 1080, "Fast", 14, 30, 40), 60, 2))
            compare(CardTest.best(root.run(1920, 1080, "Fast", 5, 10, 15), 30), 4)
            compare(CardTest.best(root.run(1920, 1080, "Fast", 5, 10, 15), 60), 3)
            compare(CardTest.best(root.run(1920, 1080, "Fast", 20, 28, 30), 30), 2, "3X does not fit, so 4X is not counted either")
            compare(CardTest.best(root.run(1920, 1080, "Fast", 30, 60, 90), 30), 0)
            compare(CardTest.best({}, 30), 0, "no timings: nothing fits")
            compare(CardTest.best({ pairWallMs: { "2x": { p95: 0 } } }, 30), 0, "a zero is not a timing")
            const rows = CardTest.rows(root.fastCard)
            compare(rows.length, 6)
            compare(rows[0].size, "1080p"); compare(rows[3].size, "4K")
            compare(rows[5].for30, 4, "4K Quality: 24 ms for three frames fits a 30 FPS game")
            compare(rows[5].for60, 2)
            compare(CardTest.sizeLabel({ width: 2560, height: 1440 }), "1440p")
            compare(CardTest.sizeLabel({ width: 1280, height: 720 }), "1280 × 720")
            compare(CardTest.rows(null).length, 0)
            compare(CardTest.rows([null, 3, {}]).length, 1)
            compare(CardTest.tierFor(root.fastCard, 2160, 30, 4), "Quality", "the best-looking tier that reaches 4X")
            compare(CardTest.tierFor(root.slowCard, 2160, 30, 4), "", "none reaches 4X")
            compare(CardTest.tierFor(root.slowCard, 2160, 30, 2), "Balanced", "20 ms for one frame fits; Quality's 50 ms does not")
            compare(CardTest.tierFor(root.slowCard, 2160, 60, 2), "Fast")
            compare(CardTest.tierFor(root.slowCard, 1440, 30, 2), "", "a size that was not tested")
        }

        // --- the card test dialog ---------------------------------------------------
        function test_card_test_runs_and_shows_the_result() {
            host.dialog = "cardtest"
            verify(card.shown && !card.running && !card.done)
            compare(labels(card).length, 2)
            card.actionTriggered(labels(card)[1])
            compare(veyra.cardTestStarts, 1)
            verify(card.running)
            verify(control(card, "pushfps-card-test-timer").running, "the dialog keeps asking while it runs")
            compare(labels(card).length, 1, "only Cancel while it runs")
            // The benchmark finishes.
            veyra.cardTestState = ({running: false, done: true, ok: true, adapter: "NVIDIA GeForce RTX 5070 Ti", vramMB: 16303, runs: root.fastCard,
                                    text: "{\n  \"pushfps\": \"3.2.2\",\n  \"adapter\": \"NVIDIA GeForce RTX 5070 Ti\"\n}\n"})
            control(card, "pushfps-card-test-timer").triggered()
            verify(card.ok && !card.running)
            compare(card.rows.length, 6)
            verify(card.headline.indexOf("120") >= 0, card.headline)
            verify(control(card, "pushfps-card-test-adapter").text.indexOf("RTX 5070 Ti") >= 0)
            verify(control(card, "pushfps-card-test-adapter").text.indexOf("16 GB") >= 0)
            compare(labels(card).length, 3)
            card.actionTriggered(labels(card)[2])
            verify(veyra.lastCopiedText.indexOf("RTX 5070 Ti") >= 0, "the result is copied")
            verify(card.copied)
            veyra.lastCopiedText = ""
            card.actionTriggered(labels(card)[1])
            compare(veyra.lastOpenedLink, "issues")
            verify(veyra.lastCopiedText.length > 0)
            card.actionTriggered(labels(card)[0])
            compare(host.dialog, "")
        }
        function test_card_test_on_a_slow_card_says_what_still_works() {
            veyra.cardTestState = ({running: false, done: true, ok: true, adapter: "Old card", vramMB: 4096, runs: root.slowCard, text: "{}"})
            host.dialog = "cardtest"
            verify(card.ok)
            verify(card.headline.indexOf("60") >= 0 && card.headline.indexOf("120") < 0, card.headline)
            // Nothing fits at 4K at all.
            veyra.cardTestState = ({running: false, done: true, ok: true, adapter: "Older card", vramMB: 2048, text: "{}",
                                    runs: [root.run(3840, 2160, "Fast", 40, 80, 120), root.run(3840, 2160, "Quality", 90, 180, 270)]})
            card.refresh()
            verify(card.headline.indexOf("1080p") >= 0, card.headline)
        }
        function test_card_test_refusals_and_cancel() {
            // Something is playing: the bridge refuses, the dialog says why.
            veyra.cardTestNext = ({running: false, done: true, ok: false, reason: "playing"})
            host.dialog = "cardtest"
            card.actionTriggered(labels(card)[1])
            verify(!card.running && card.done && !card.ok)
            verify(card.reasonText.length > 0)
            compare(control(card, "pushfps-card-test-state").text, card.reasonText)
            compare(labels(card).length, 2, "Close and Test again")
            for (const reason of ["missing", "not-started", "no-pushflow", "stopped", "cancelled"]) {
                veyra.cardTestState = ({running: false, done: true, ok: false, reason: reason})
                card.refresh()
                verify(card.reasonText.length > 0, reason)
            }
            // Running, then cancelled.
            veyra.cardTestNext = ({running: true, done: false})
            card.actionTriggered(labels(card)[1])
            verify(card.running)
            card.actionTriggered(labels(card)[0])
            compare(veyra.cardTestCancels, 1)
            verify(!card.running && card.done && !card.ok)
            card.copy()
            compare(veyra.lastCopiedText, "", "nothing to copy without a result")
        }
    }
}
