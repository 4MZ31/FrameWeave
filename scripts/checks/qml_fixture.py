#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
"""The interface tests' bridge (the QML object in tests/qml/QuickSmokeTests.cpp) must load.

Every test in tests/qml/quick shares that one object. When it does not load, no test runs, and the
Windows build says so about forty minutes in. This check reads the object's text and fails on the
mistakes that have stopped it:

  * a name declared twice (two properties, two functions, a property and a function);
  * a signal named after a property's own change signal (`property var x` already has `xChanged`;
    21 November 2026: `signal pushReadTextChanged()` beside `property var pushReadText`);
  * a piece of the text longer than a string literal may be for MSVC (16380 bytes).

It reads declarations by their indentation, as the file writes them (members of the object at four
spaces), so it finds what it is written for and nothing else: the tests themselves run in the
build-windows and qml-tests-fast workflows.

Usage: python3 -B scripts/checks/qml_fixture.py [path to QuickSmokeTests.cpp]
"""
import collections
import pathlib
import re
import sys

LIMIT = 16380


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[2]
    path = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "tests" / "qml" / "QuickSmokeTests.cpp"
    source = path.read_text(encoding="utf-8")
    start = source.find("snapshot.setData(")
    if start < 0:
        print(f"{path}: the bridge object was not found (snapshot.setData)")
        return 1
    end = source.find(", QUrl());", start)
    pieces = re.findall(r'R"qml\((.*?)\)qml"', source[start:end if end > 0 else len(source)], re.S)
    if not pieces:
        print(f"{path}: no raw string pieces found")
        return 1
    problems = []
    for number, piece in enumerate(pieces, 1):
        size = len(piece.encode("utf-8"))
        if size > LIMIT:
            problems.append(f"piece {number} is {size} bytes; MSVC takes at most {LIMIT} in one string literal: "
                            f'split it with )qml" and R"qml(')
    text = "".join(pieces)
    kinds = collections.defaultdict(list)   # name -> ["property" | "function" | "signal", ...]
    for line in text.split("\n"):
        hit = re.match(r"^    (?:readonly |required |default )*property\s+[\w<>.]+\s+(\w+)", line)
        if hit:
            kinds[hit.group(1)].append("property")
            continue
        hit = re.match(r"^    function\s+(\w+)\s*\(", line)
        if hit:
            kinds[hit.group(1)].append("function")
            continue
        hit = re.match(r"^    signal\s+(\w+)", line)
        if hit:
            kinds[hit.group(1)].append("signal")
    for name, found in sorted(kinds.items()):
        if len(found) > 1:
            problems.append(f"`{name}` is declared {len(found)} times ({', '.join(found)})")
    properties = {name for name, found in kinds.items() if "property" in found}
    for name, found in sorted(kinds.items()):
        if "signal" in found and name.endswith("Changed") and name[:-len("Changed")] in properties:
            problems.append(f"`signal {name}` repeats the change signal `property {name[:-len('Changed')]}` already has")
    if problems:
        print(f"{path.name}: the tests' bridge object would not load:")
        for problem in problems:
            print("  - " + problem)
        return 1
    print(f"{path.name}: {len(kinds)} members in {len(pieces)} pieces, largest "
          f"{max(len(p.encode('utf-8')) for p in pieces)} bytes: no doubled name, no repeated change signal")
    return 0


if __name__ == "__main__":
    sys.exit(main())
