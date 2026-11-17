// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// The Frame Input API dialog (PushFrameInputDialog: senders found, Use, another channel, the docs
// link) against the fixture bridge.
import QtQuick
import QtTest
import Veyra
Item {
    id: root
    width: 900; height: 900
    Item { id: host; anchors.fill: parent; property string dialog: ""; function close() { dialog = "" } }
    PushFrameInputDialog { id: input; dialogHost: host }

    TestCase {
        name: "PushFrameInput"
        when: windowShown
        function init() {
            host.dialog = ""
            veyra.frameInputSenders = []
            veyra.frameInputStarted = ""
            veyra.lastOpenedLink = ""
        }
        function test_nothing_sending_says_so() {
            host.dialog = "frameinput"
            verify(input.shown)
            tryVerify(() => input.senders.length === 0 && findChild(input, "frame-input-none").visible, 3000, "the empty state is shown")
            verify(!input.use("default"), "nothing to open")
            compare(host.dialog, "frameinput", "the dialog stays open")
        }
        function test_a_sender_is_listed_and_used() {
            veyra.frameInputSenders = [{ channel: "default", name: "My emulator", width: 1920, height: 1080, fps: 60, alive: true, frames: 10, pid: 4 },
                                       { channel: "2", name: "Old app", width: 1280, height: 720, fps: 0, alive: false, frames: 3, pid: 5 }]
            host.dialog = "frameinput"
            compare(input.senders.length, 2)
            waitForRendering(input)
            verify(findChild(input, "frame-input-sender-default") !== null)
            verify(!findChild(input, "frame-input-use-2").enabled, "a closed sender can't be used")
            verify(input.use("default"))
            compare(veyra.frameInputStarted, "default")
            compare(host.dialog, "", "the dialog closes once the source opens")
        }
        function test_docs_link() {
            host.dialog = "frameinput"
            findChild(input, "frame-input-docs").clicked()
            compare(veyra.lastOpenedLink, "frameInputDocs")
        }
    }
}
