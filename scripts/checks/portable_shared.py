#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
"""The interface files the Linux and macOS preview shares with the Windows build.

The preview compiles a list of files straight from qml/Veyra (portable/app/CMakeLists.txt,
`pushfps_shared_names`). On Windows they read the bridge, the context object `veyra`; in the preview
`veyra` is a small object of its own (portable/app/src/VeyraShim.h) that answers a few names. A change
to one of those files that reads another name works on Windows and reads `undefined` in the preview,
and nothing on the Windows side says so: the preview is only built by its own workflow.

This check lists every `veyra.<name>` the shared files read and fails for a name the preview's object
does not have, unless the line itself guards the read (`typeof veyra`), or the name is one of the few
listed below with the reason it is safe. It also fails for a shared file that does not exist.

Run: python3 -B scripts/checks/portable_shared.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CMAKE = ROOT / "portable/app/CMakeLists.txt"
SHIM = ROOT / "portable/app/src/VeyraShim.h"
SHARED = ROOT / "qml/Veyra"

# Reads that are safe without the preview's object answering them, each with why.
ALLOWED = {
}


def shared_names():
    text = CMAKE.read_text(encoding="utf-8")
    m = re.search(r"set\(pushfps_shared_names\s+([^)]*)\)", text)
    if not m:
        sys.exit("portable_shared: no pushfps_shared_names list in portable/app/CMakeLists.txt")
    return m.group(1).split()


def shim_members():
    text = SHIM.read_text(encoding="utf-8")
    names = set(re.findall(r"Q_PROPERTY\(\s*[\w:<>]+\s+(\w+)", text))
    names |= set(re.findall(r"Q_INVOKABLE\s+[\w:<>&\s]+?\b(\w+)\s*\(", text))
    names |= set(re.findall(r"^\s*void\s+(\w+)\s*\(\s*\)\s*;", text[text.find("signals:"):text.find("private:")], re.M))
    return names


def strip_comments(line):
    return re.sub(r"//.*$", "", line)


def main():
    members = shim_members()
    problems = []
    reads = 0
    names = shared_names()
    for name in names:
        path = SHARED / name
        if not path.exists():
            problems.append(f"{name}: listed in pushfps_shared_names but not in qml/Veyra")
            continue
        for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            line = strip_comments(raw)
            for member in re.findall(r"\bveyra\.(\w+)", line):
                reads += 1
                if member in members or member in ALLOWED:
                    continue
                if "typeof veyra" in line:
                    continue
                # a signal handler of the bridge: Connections { target: veyra; function onXChanged() ... } is not a read
                problems.append(f"{name}:{number}: reads veyra.{member}, which the preview's object (VeyraShim.h) does not have")
    if problems:
        print("\n".join(problems))
        print(f"portable_shared: {len(problems)} problem(s). Guard the read with `typeof veyra`, or add the name to VeyraShim.h.")
        return 1
    print(f"portable_shared: {len(names)} shared files, {reads} reads of the bridge, all answered by the preview's object ({len(members)} names)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
