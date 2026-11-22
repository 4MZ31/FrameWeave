#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
"""A settings row lays out ONE control.

VRow (qml/Veyra/VRow.qml) puts whatever stands in its slot at the same place: two buttons written
straight into a row are drawn on top of each other. That is how "Check for updates" showed "View
update" over "Install update" (a user's screenshot, 22 November 2026). Several controls belong in
one RowLayout (or Row, Flow, ColumnLayout ...) inside the row.

This finds every VRow in qml/ with more than one visual child of its own and fails. It reads the
files by their braces, with comments and strings taken out, so it needs no Qt.
Usage: python3 -B scripts/checks/qml_rows.py [folder ...]
"""
import os
import re
import sys

# Things that stand in a row without being drawn in its slot.
NOT_DRAWN = {"Timer", "Connections", "Binding", "QtObject", "FontMetrics", "TextMetrics", "HoverHandler",
             "TapHandler", "DragHandler", "PointHandler", "WheelHandler", "Shortcut", "Component", "SignalSpy",
             "NumberAnimation", "SequentialAnimation", "ParallelAnimation", "Behavior", "States", "State"}
OPEN = re.compile(r"^(\s*)(?:[a-zA-Z_][\w.]*\s*:\s*)?([A-Z][\w.]*)\s*(?:on\s+[\w.]+\s*)?\{")


def bare(text):
    """The file without comments and without the insides of strings, line for line."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
            continue
        if c in "\"'`":
            q = c
            i += 1
            while i < n and text[i] != q:
                if text[i] == "\\":
                    i += 1
                if i < n and text[i] == "\n":
                    out.append("\n")
                i += 1
            i += 1
            out.append('""')
            continue
        out.append(c)
        i += 1
    return "".join(out)


def rows(path):
    """Every VRow with its own visual children, by braces (not by how the file is indented)."""
    lines = bare(open(path, encoding="utf-8").read()).split("\n")
    found = []
    depth = 0
    open_rows = []          # [depth inside the row, line, children]
    for number, text in enumerate(lines, 1):
        c = OPEN.match(text)
        if c and open_rows and depth == open_rows[-1][0]:
            kind = c.group(2)
            before = text[: c.start(2)].strip()
            if not before.endswith(":") and kind.split(".")[-1] not in NOT_DRAWN:
                open_rows[-1][2].append((number, kind))
        is_row = re.match(r"^\s*VRow\s*\{", text) is not None
        for ch in text:
            if ch == "{":
                depth += 1
                if is_row:
                    open_rows.append([depth, number, []])
                    is_row = False
            elif ch == "}":
                if open_rows and depth == open_rows[-1][0]:
                    row = open_rows.pop()
                    if len(row[2]) > 1:
                        found.append((row[1], row[2]))
                depth -= 1
    return sorted(found)


def main(argv):
    roots = argv[1:] or ["qml", "portable/app"]
    problems = 0
    count = 0
    for root in roots:
        for folder, _, names in os.walk(root):
            for name in sorted(names):
                if not name.endswith(".qml"):
                    continue
                path = os.path.join(folder, name)
                for line, children in rows(path):
                    problems += 1
                    print(f"{path}:{line}: a row with {len(children)} controls of its own: "
                          + ", ".join(f"{kind} (line {n})" for n, kind in children)
                          + ". Put them in one RowLayout inside the row.")
                count += open(path, encoding="utf-8").read().count("VRow {")
    print(f"rows: {count} checked, {problems} with more than one control of their own")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
