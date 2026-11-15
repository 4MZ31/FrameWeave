#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
"""Steam store and library art in the Sunset City look, at the sizes Steamworks asks for.

Built from the same procedural pieces as scripts/brand/make_sunset.py (no game artwork, no
trademarks). Steam's rules for capsules: the product name or logo only, no review scores, awards,
prices or other text. Writes into packaging/steam/art/. Run: python3 scripts/brand/make_steam.py
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

import make_sunset as ms

OUT = ms.ROOT / "packaging/steam/art"

# name: (width, height, with logo)
SIZES = {
    "header_capsule": (920, 430, True),
    "small_capsule": (462, 174, True),
    "main_capsule": (1232, 706, True),
    "vertical_capsule": (748, 896, True),
    "page_background": (1438, 810, False),
    "library_capsule": (600, 900, True),
    "library_header": (920, 430, True),
    "library_hero": (3840, 1240, False),
}


def road_floor(w, h, horizon):
    """Dark ground with a neon road grid towards a vanishing point in the middle."""
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ground = ms.gradient(w, h - horizon, [(0, (44, 10, 64)), (1, (11, 4, 24))])
    img.alpha_composite(ground, (0, horizon))
    grid = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(grid)
    depth = h - horizon
    lw = max(1, round(h / 500))
    for i in range(-18, 19):
        d.line(((w / 2 + i * w * 0.004, horizon), (w / 2 + i * w * 0.1, h)), fill=(255, 79, 168, 150), width=lw)
    for i in range(1, 15):
        p = i / 14
        y = horizon + depth * p ** 2.4
        d.line(((0, y), (w, y)), fill=(255, 95, 180, int(60 + 150 * p)), width=max(1, round(lw * (0.6 + p * 1.6))))
    fade = ms.gradient(1, h, [(0, (0, 0, 0)), (horizon / h, (0, 0, 0)), (1, (255, 255, 255))]).convert("L").resize((w, h))
    grid.putalpha(Image.composite(grid.split()[3], Image.new("L", (w, h), 0), fade))
    img.alpha_composite(grid.filter(ImageFilter.GaussianBlur(lw * 0.4)))
    glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(glow).rectangle((0, horizon - lw * 2, w, horizon + lw * 2), fill=(255, 180, 215, 230))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(lw * 3)))
    return img


def scene(w, h, horizon_frac=0.64):
    horizon = int(h * horizon_frac)
    img = ms.gradient(w, h, ms.SKY, 0, horizon)
    unit = min(w, h * 1.6)
    sd = ms.sun(int(unit * 0.36), glow=True)
    img.alpha_composite(sd, (int(w / 2 - sd.width / 2), int(horizon - sd.height * 0.62)))
    back = ms.skyline(w, int(h * 0.26), 31, ms.BUILDING_BACK, windows=False, density=1.1)
    img.alpha_composite(back, (0, horizon - back.height))
    front = ms.skyline(w, int(h * 0.18), 47, ms.BUILDING_FRONT, windows=True, max_frac=0.9)
    img.alpha_composite(front, (0, horizon - front.height))
    img.alpha_composite(road_floor(w, h, horizon))
    ph = int(min(h * 0.75, w * 0.6))
    left = ms.palm(ph, lean=0.2, seed=13)
    img.alpha_composite(left, (int(-left.width * 0.28), horizon + int(h * 0.12) - ph))
    right = ms.palm(int(ph * 0.85), lean=0.16, seed=7).transpose(Image.FLIP_LEFT_RIGHT)
    img.alpha_composite(right, (w - int(right.width * 0.72), horizon + int(h * 0.12) - right.height))
    return img


def logo_art():
    """The sunset-filled logo with its glow, as on the app's home page (transparent)."""
    m = ms.masks()
    full = ms.filled(m["all"], ms.letter_fill(ms.LW * ms.S, ms.LH * ms.S))
    pad = 140
    art = ms.glow(m["all"], pad)
    art.alpha_composite(full, (pad, pad))
    return art


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    logo = logo_art()
    for name, (w, h, with_logo) in SIZES.items():
        tall = h > w
        img = scene(w, h, 0.72 if tall else 0.64)
        if with_logo:
            # A dark haze behind the logo: its fill uses the same sunset colours as the scene.
            cy = h * (0.3 if tall else 0.42)
            shade = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            rx, ry = w * (0.44 if tall else 0.4), h * (0.13 if tall else 0.24)
            ImageDraw.Draw(shade).ellipse((w / 2 - rx, cy - ry, w / 2 + rx, cy + ry), fill=(18, 6, 36, 175))
            img.alpha_composite(shade.filter(ImageFilter.GaussianBlur(min(w, h) * 0.09)))
            lw = int(w * (0.92 if tall else (0.96 if w < 500 else 0.78)))
            lh = int(logo.height * lw / logo.width)
            l = logo.resize((lw, lh), Image.LANCZOS)
            img.alpha_composite(l, (int(w / 2 - lw / 2), int(cy - lh / 2)))
        img.convert("RGB").save(OUT / f"{name}.png", optimize=True)
    # Library logo: the logo alone on transparency, 1280x720.
    canvas = Image.new("RGBA", (1280, 720), (0, 0, 0, 0))
    lw = 1240
    l = logo.resize((lw, int(logo.height * lw / logo.width)), Image.LANCZOS)
    canvas.alpha_composite(l, ((1280 - l.width) // 2, (720 - l.height) // 2))
    canvas.save(OUT / "library_logo.png", optimize=True)
    # Community icon 184x184 and client icon: the app icon.
    icon = Image.open(ms.ROOT / "assets/pushfps-icon.png").convert("RGBA")
    if icon:
        bg = Image.new("RGBA", (184, 184), (21, 10, 40, 255))
        bg.alpha_composite(icon.resize((164, 164), Image.LANCZOS), (10, 10))
        bg.convert("RGB").save(OUT / "community_icon.png", optimize=True)
    print("steam art written to", OUT)


if __name__ == "__main__":
    main()
