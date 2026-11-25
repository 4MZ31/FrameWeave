#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
"""Render the interlaced FrameWeave frames at desktop and launcher icon sizes."""
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]


def mark(size, tile=False):
    # Work in a 64-unit grid. The gap at one crossing puts the back frame
    # in front there, so the two outlines alternate like a weave.
    scale = size * 4 / 64
    image = Image.new("RGBA", (size * 4, size * 4))
    draw = ImageDraw.Draw(image)

    def box(points):
        return tuple(round(v * scale) for v in points)

    stroke = max(1, round(4.5 * scale))
    if tile:
        draw.rounded_rectangle(box((0, 0, 64, 64)), radius=round(13 * scale), fill="#111820")
    back, front = "#559C84", "#A7EF64"
    draw.rounded_rectangle(box((9, 13, 42, 39)), radius=round(6 * scale), outline=back, width=stroke)
    draw.rounded_rectangle(box((22, 25, 55, 51)), radius=round(6 * scale), outline=front, width=stroke)
    draw.rectangle(box((20, 35, 29, 39)), fill=back)
    return image.resize((size, size), Image.Resampling.LANCZOS)


if __name__ == "__main__":
    icon = mark(1024, tile=True)
    icon.save(ROOT / "portable/app/qml/icon.png", optimize=True)
    mark(512, tile=True).save(ROOT / "assets/veyra-app-icon.png", optimize=True)
    sizes = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
    icon.save(ROOT / "assets/veyra.ico", sizes=[(s, s) for s in sizes])
    logo = mark(360)
    for folder in ("qml/Veyra", "portable/app/qml"):
        logo.save(ROOT / folder / "logo.png", optimize=True)
