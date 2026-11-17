#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
"""Draws the PushFlow logo (PushFPS's own frame generation). Change the shape here, never by hand.

The mark: three slanted frames, the way PushFPS's chevrons lean forward. The outer two are the real frames
(the faint one is the frame before, the solid one the frame after); the middle one is the frame PushFlow
makes, outlined, with a spark. A flow line sweeps under all three from the first frame to the last.
On light backgrounds the spark and the flow line are graphite. Writes assets/pushflow-mark.png, assets/pushflow-logo.png (for dark backgrounds),
assets/pushflow-logo-light.png (for light backgrounds) and the website's .webp copies.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
GREEN = (141, 242, 55, 255)        # logo green #8df237
GRAPHITE = (27, 29, 34, 255)
WHITE = (242, 244, 240, 255)
SS = 4                             # supersampling


def slanted(x, y, w, h, lean):
    return [(x + lean, y), (x + w + lean, y), (x + w, y + h), (x, y + h)]


def mark(size: int, ink=WHITE) -> Image.Image:
    s = size * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    u = s / 256.0
    h, w, lean, top = 132 * u, 50 * u, 26 * u, 34 * u
    xs = [22 * u, 92 * u, 162 * u]
    # Real frame before (faint), generated frame (outline + spark), real frame after (solid).
    d.polygon(slanted(xs[0], top, w, h, lean), fill=GREEN[:3] + (92,))
    gen = slanted(xs[1], top, w, h, lean)
    d.polygon(gen, fill=GREEN[:3] + (36,))
    d.line(gen + [gen[0]], fill=GREEN, width=int(9 * u), joint="curve")
    d.polygon(slanted(xs[2], top, w, h, lean), fill=GREEN)
    # Spark on the generated frame: a four-point star.
    cx, cy, r, k = xs[1] + w / 2 + lean / 2, top + h / 2, 21 * u, 5 * u
    d.polygon([(cx, cy - r), (cx + k, cy - k), (cx + r, cy), (cx + k, cy + k), (cx, cy + r), (cx - k, cy + k), (cx - r, cy), (cx - k, cy - k)], fill=ink)
    # Flow line: a shallow arc under the frames ending in an arrow head.
    y0, y1 = top + h + 30 * u, top + h + 18 * u
    pts = []
    for i in range(65):
        t = i / 64
        x = 18 * u + t * 200 * u
        y = y0 * (1 - t) ** 2 + (y0 + 26 * u) * 2 * t * (1 - t) + y1 * t * t
        pts.append((x, y))
    d.line(pts, fill=ink, width=int(10 * u), joint="curve")
    ex, ey = pts[-1]
    d.polygon([(ex + 22 * u, ey - 6 * u), (ex - 4 * u, ey - 22 * u), (ex - 2 * u, ey + 12 * u)], fill=ink)
    return img.resize((size, size), Image.LANCZOS)


def logo(height: int, text_colour) -> Image.Image:
    s = height * SS
    font = ImageFont.truetype(str(ROOT / "website/fonts/BarlowSemiCondensed-ExtraBoldItalic.woff2"), int(s * 0.62))
    m = mark(height, text_colour).resize((s, s), Image.LANCZOS)
    probe = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
    push_w = probe.textlength("Push", font=font)
    flow_w = probe.textlength("Flow", font=font)
    gap = int(s * 0.12)
    width = s + gap + int(push_w + flow_w) + int(s * 0.08)
    img = Image.new("RGBA", (width, s), (0, 0, 0, 0))
    img.alpha_composite(m, (0, 0))
    d = ImageDraw.Draw(img)
    ascent, descent = font.getmetrics()
    y = (s - (ascent + descent)) // 2 + int(s * 0.02)
    d.text((s + gap, y), "Push", font=font, fill=text_colour)
    d.text((s + gap + push_w, y), "Flow", font=font, fill=GREEN)
    return img.resize((width // SS, height), Image.LANCZOS)


def main() -> None:
    assets, web = ROOT / "assets", ROOT / "website" / "assets"
    m = mark(512)
    m.save(assets / "pushflow-mark.png", optimize=True)
    dark = logo(160, WHITE)
    dark.save(assets / "pushflow-logo.png", optimize=True)
    light = logo(160, GRAPHITE)
    light.save(assets / "pushflow-logo-light.png", optimize=True)
    m.resize((160, 160), Image.LANCZOS).save(web / "pushflow-mark.webp", quality=92, method=6)
    logo(96, WHITE).save(web / "pushflow-logo.webp", quality=92, method=6)
    logo(96, GRAPHITE).save(web / "pushflow-logo-light.webp", quality=92, method=6)
    # Preview on both backgrounds (not committed).
    preview = Image.new("RGBA", (dark.width + 40, dark.height * 2 + 60), GRAPHITE)
    preview.alpha_composite(dark, (20, 20))
    band = Image.new("RGBA", (preview.width, dark.height + 20), (246, 247, 243, 255))
    preview.alpha_composite(band, (0, dark.height + 40))
    preview.alpha_composite(light, (20, dark.height + 50))
    preview.save(Path("/tmp/pushflow-preview.png"))


if __name__ == "__main__":
    main()
