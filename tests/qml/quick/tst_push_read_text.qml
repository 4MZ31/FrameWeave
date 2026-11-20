// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// Read the text on the picture (PushReadTextDialog), against the fixture bridge: what the dialog
// asks for, shows, copies and sends to be translated. The reading itself is Windows' and is run in
// veyra_push_ocr_windows_tests; the arithmetic in veyra_push_ocr_tests.
import QtQuick
import QtTest
import Veyra
Item {
    id: root
    width: 900; height: 1400
    Item { id: host; anchors.fill: parent; property string dialog: ""; property real motionScale: 1; function close() { dialog = "" } }
    PushReadTextDialog { id: read; dialogHost: host }
    property string navigated: ""
    Connections { target: veyra; function onNavigate(p) { root.navigated = p } }

    TestCase {
        name: "PushReadText"
        when: windowShown
        property var saved: ({})
        readonly property var translation: veyra.liveTranslation
        function init() {
            saved = { hasSource: veyra.hasSource, preferences: veyra.preferences, answer: veyra.readTextAnswer, provider: translation.provider,
                      source: translation.sourceLanguage, target: translation.targetLanguage, installed: translation.modelInstalled,
                      downloading: translation.downloading, apiBase: translation.apiBase, apiModel: translation.apiModel, supported: translation.supported }
            host.dialog = ""; root.navigated = ""
            veyra.hasSource = true
            veyra.pushReadText = ({ phase: "idle" })
            veyra.readTextStarts = []; veyra.lastCopiedText = ""
            translation.provider = 0; translation.modelInstalled = true; translation.downloading = false
            translation.sourceLanguage = ""; translation.targetLanguage = "en"; translation.onceAsked = []; translation.clearOnce()
        }
        function cleanup() {
            host.dialog = ""
            veyra.hasSource = saved.hasSource; veyra.preferences = saved.preferences; veyra.readTextAnswer = saved.answer
            translation.provider = saved.provider; translation.sourceLanguage = saved.source; translation.targetLanguage = saved.target
            translation.modelInstalled = saved.installed; translation.downloading = saved.downloading
            translation.apiBase = saved.apiBase; translation.apiModel = saved.apiModel; translation.supported = saved.supported
            translation.clearOnce()
            veyra.pushReadText = ({ phase: "idle" })
        }
        function control(name) { const c = findChild(read, name); verify(c !== null, name); return c }
        function open() {
            host.dialog = "readtext"
            tryVerify(function() { return read.shown }, 3000)
        }

        function test_opening_it_with_a_picture_reads_at_once_and_shows_the_text() {
            const prepared = veyra.readTextPrepared
            open()
            compare(veyra.readTextPrepared, prepared + 1, "Windows is asked which languages it reads")
            compare(veyra.readTextStarts.length, 1, "opening it with something playing is asking")
            compare(veyra.readTextStarts[0].tag, "", "the user's own languages unless one was chosen")
            compare(veyra.readTextStarts[0].region, 0, "the whole picture unless a part was chosen")
            compare(read.phase, "done")
            compare(read.found, "New game\nOptions")
            tryVerify(function() { return control("pushfps-readtext-box").visible }, 3000)
            compare(control("pushfps-readtext-text").text, "New game\nOptions")
            verify(control("pushfps-readtext-state").text.indexOf("2") >= 0, "it says how many lines")
            // Copy copies exactly what was read.
            control("pushfps-readtext-copy").clicked()
            compare(veyra.lastCopiedText, "New game\nOptions")
            compare(read.copied, "text")
            // Read again reads again.
            control("pushfps-readtext-read").clicked()
            compare(veyra.readTextStarts.length, 2)
        }
        function test_the_language_and_the_part_chosen_are_what_is_read_and_are_remembered() {
            open()
            compare(read.languageOptions.length, 3, "automatic and the two languages Windows has")
            control("pushfps-readtext-language").picked("ja")
            compare(veyra.preferences.pushReadLanguage, "ja")
            control("pushfps-readtext-region").picked("2")
            compare(veyra.preferences.pushReadRegion, 2)
            control("pushfps-readtext-read").clicked()
            const last = veyra.readTextStarts[veyra.readTextStarts.length - 1]
            compare(last.tag, "ja"); compare(last.region, 2)
            // A language saved earlier that Windows no longer has is not asked for.
            const prefs = Object.assign({}, veyra.preferences); prefs.pushReadLanguage = "ko"; veyra.preferences = prefs
            compare(read.language, "")
            control("pushfps-readtext-read").clicked()
            compare(veyra.readTextStarts[veyra.readTextStarts.length - 1].tag, "")
            // A part that is no part is the whole picture.
            const odd = Object.assign({}, veyra.preferences); odd.pushReadRegion = 9; veyra.preferences = odd
            compare(read.region, 0)
            control("pushfps-readtext-language-settings").clicked()
            verify(veyra.languageSettingsOpened > 0)
        }
        function test_nothing_is_read_without_a_picture_and_what_went_wrong_is_said() {
            veyra.hasSource = false
            open()
            compare(veyra.readTextStarts.length, 0, "nothing playing: nothing is asked")
            verify(!control("pushfps-readtext-read").enabled)
            verify(!control("pushfps-readtext-box").visible)
            verify(!control("pushfps-readtext-translate-group").visible, "nothing to translate")
            // Opened before the picture started: it reads once the picture is there, and only once.
            veyra.hasSource = true
            compare(veyra.readTextStarts.length, 1)
            veyra.hasSource = false; veyra.hasSource = true
            compare(veyra.readTextStarts.length, 1, "what was read is not read again by itself")
            host.dialog = ""
            veyra.readTextStarts = []
            // A frame without text, and Windows without a language to read with.
            veyra.hasSource = true
            veyra.readTextAnswer = ({ phase: "done", error: "", text: "", count: 0, lines: [], language: "en-US" })
            open()
            compare(read.phase, "done"); compare(read.found, "")
            verify(control("pushfps-readtext-state").text.length > 0)
            verify(!control("pushfps-readtext-box").visible && !control("pushfps-readtext-copy").visible)
            veyra.readTextAnswer = ({ phase: "failed", error: "no-language", text: "", count: 0, lines: [] })
            const none = control("pushfps-readtext-state").text
            control("pushfps-readtext-read").clicked()
            compare(read.phase, "failed")
            verify(control("pushfps-readtext-state").text !== none, "the reason is its own sentence")
        }
        function test_a_translation_is_asked_for_with_its_own_button_and_says_where_the_text_goes() {
            open()
            tryVerify(function() { return control("pushfps-readtext-translate-group").visible }, 3000)
            compare(translation.onceAsked.length, 0, "reading never translates by itself")
            // The local model: the text stays on this PC.
            compare(read.ready, 0); verify(!read.online)
            const local = control("pushfps-readtext-engine-row").hint
            control("pushfps-readtext-translate").clicked()
            compare(translation.onceAsked.length, 1)
            compare(translation.onceAsked[0], "New game\nOptions", "exactly what was read")
            tryVerify(function() { return control("pushfps-readtext-translated-box").visible }, 3000)
            compare(control("pushfps-readtext-translated").text, "[en] New game\nOptions")
            control("pushfps-readtext-translate-copy").clicked()
            compare(veyra.lastCopiedText, "[en] New game\nOptions")
            // Another language to translate into.
            control("pushfps-readtext-target").picked("ja")
            compare(translation.targetLanguage, "ja")
            // An online engine: said before anything is sent, and it needs the text's language.
            control("pushfps-readtext-engine").picked("2")
            compare(translation.provider, 2)
            compare(read.ready, 3, "MyMemory has to be told the language of the text")
            verify(!control("pushfps-readtext-translate").enabled)
            tryVerify(function() { return control("pushfps-readtext-source").visible }, 3000)
            control("pushfps-readtext-source").picked("ja")
            compare(read.ready, 0); verify(read.online)
            verify(control("pushfps-readtext-engine-row").hint !== local, "it says the text will be sent")
            verify(control("pushfps-readtext-engine-row").hint.indexOf("MyMemory") >= 0)
            compare(translation.onceAsked.length, 1, "choosing an engine sends nothing")
        }
        function test_the_local_model_is_downloaded_from_here_when_it_is_missing() {
            translation.modelInstalled = false
            open()
            tryVerify(function() { return control("pushfps-readtext-translate-group").visible }, 3000)
            compare(read.ready, 1)
            verify(!control("pushfps-readtext-translate").enabled)
            tryVerify(function() { return control("pushfps-readtext-model").visible }, 3000)
            control("pushfps-readtext-model").clicked()
            compare(translation.downloading, true)
            compare(read.ready, 4)
            control("pushfps-readtext-model").clicked()
            compare(translation.downloading, false, "the same button cancels it")
            // The custom API is only offered where it is set up, or where its own dialog can be opened.
            translation.supported = false; translation.apiBase = ""
            compare(read.engines.length, 2)
            translation.apiBase = "https://example.com/v1"
            compare(read.engines.length, 3)
        }
        function test_closing_it_forgets_the_text() {
            open()
            control("pushfps-readtext-translate").clicked()
            verify(read.translated.length > 0)
            const cleared = veyra.readTextCleared
            host.dialog = ""
            tryVerify(function() { return !read.shown }, 3000)
            compare(veyra.readTextCleared, cleared + 1)
            compare(read.found, ""); compare(read.translated, "")
        }
    }
}
