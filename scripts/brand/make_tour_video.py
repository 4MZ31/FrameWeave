#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#
# The 4.4.5 tour video: where the new things are. It is the 4.3 film's machinery (make_release_video.py:
# the window, the camera, the light round a part, the words, the parts and the join) with its own scenes,
# cut to the same music and so to the same beats. Same commands:
#
#   python3 scripts/brand/make_tour_video.py --frame 12.5 frame.png
#   python3 scripts/brand/make_tour_video.py --list
#   python3 scripts/brand/make_tour_video.py --parts out/tour [n ...]
#   python3 scripts/brand/make_tour_video.py --join out/tour film.mp4 --audio music.m4a
#
# The screenshots are not in the repository. They are the T-* entries of the screenshots workflow on the
# scratch branch video/tour, 1600 x 900, into out/tour-shots (or the folder PUSHFPS_VIDEO_SHOTS names):
#   T-01-home, T-02-home-hud (--scroll-to pushfps-crosshair-row, --pref pushCrosshairHold=true),
#   T-04-nav-folded, T-06-both-folded, T-07-pro-all, T-08-pro-folded, T-11-home-playing (sender)
# The build machine has no graphics card, so its rows read "Requires an NVIDIA RTX GPU". For these
# screenshots that branch's EffectSupport.js answers "available" for everything: the rows are the real
# interface as a PC with a graphics card shows it, and nothing else of the program is changed. A playing
# picture's status strip shows the build machine's own figures: that screenshot is only used zoomed in,
# with the strip out of the frame. What cannot be photographed there (a crosshair in a game, a translated
# subtitle) is drawn, and says that it is an illustration.
import math, os, random, sys
from functools import lru_cache
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_release_video as V
from make_release_video import (W, H, FPS, LIME, T1, T2, EDGE, beat, at, clamp, out_cubic, in_out, in_out_cubic,
                                put, window, focus, highlight, caption, scrim, chip, text_sprite, words, font,
                                rim_sprite, cam_base, cam_region, cam_mix)

V.SHOTS = os.environ.get("PUSHFPS_VIDEO_SHOTS") or os.path.join(V.ROOT, "out", "tour-shots")
V.INTRO_TAG = [("4.4.5", LIME), ("   what's new, and where", T1)]
V.ALSO_HEAD = [("Also new ", T1), ("since 4.3", LIME)]
V.OUTRO_TAG = [("4.4.5", LIME), ("  is out", T1)]
V.ALSO = [("Linux and macOS", "The preview gets the new design and Crosshair Hold"),
          ("PushFlow", "The half precision test starts with one button"),
          ("PS5 Remote Play", "The bitrate row says what makes the picture sharper"),
          ("Capture cards", "AVerMedia Live Streamer Ultra HD, reported working by a user"),
          ("Updates", "No longer stopped by a file another program holds"),
          ("Motion source", "Shown for every method, and says when it does not apply")]


# ---------------------------------------------------------------- drawn pictures (no game, no screenshot)
@lru_cache(maxsize=None)
def street(w, h, seed):
    """A drawn street as a camera turning past it sees it: sky, blocks, posts, ground. It repeats after w."""
    rnd = random.Random(seed)
    im = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(im)
    horizon = int(h * 0.64)
    for y in range(h):
        if y < horizon:
            k = y / horizon
            c = (int(92 + 66 * k), int(112 + 60 * k), int(128 + 44 * k))
        else:
            k = (y - horizon) / (h - horizon)
            c = (int(72 - 26 * k), int(74 - 26 * k), int(70 - 24 * k))
        d.line((0, y, w, y), fill=c)
    x = 0
    while x < w - 60:
        bw, bh = rnd.randint(70, 190), rnd.randint(int(h * 0.16), int(h * 0.52))
        bw = min(bw, w - 20 - x)
        tone, warm = rnd.randint(46, 146), rnd.randint(-10, 16)
        d.rectangle((x, horizon - bh, x + bw, horizon + 2), fill=(tone + warm, tone + warm // 2, tone - warm))
        for _ in range(rnd.randint(3, 9)):
            wx, wy = x + rnd.randint(6, max(7, bw - 22)), horizon - rnd.randint(14, max(15, bh - 12))
            d.rectangle((wx, wy, wx + rnd.randint(8, 18), wy + rnd.randint(10, 22)), fill=(212, 206, 172) if rnd.random() < 0.5 else (58, 68, 80))
        x += bw + rnd.randint(6, 44)
    for _ in range(w // 260):
        px0, ph = rnd.randint(30, w - 30), rnd.randint(int(h * 0.22), int(h * 0.5))
        d.rectangle((px0, horizon - ph, px0 + rnd.randint(5, 10), h), fill=(36, 38, 38))
    for _ in range(w // 70):
        gx, gy = rnd.randint(0, w - 120), rnd.randint(horizon + 8, h - 6)
        d.rectangle((gx, gy, gx + rnd.randint(30, 110), gy + rnd.randint(3, 6)), fill=(98, 100, 92))
    return im


@lru_cache(maxsize=None)
def street_moving(w, h, seed, smear):
    im = street(w, h, seed)
    if smear > 1:                       # sideways only: the camera turns
        im = im.resize((max(1, w // smear), h), Image.BILINEAR).resize((w, h), Image.BILINEAR)
    im = im.filter(ImageFilter.GaussianBlur(0.8))
    two = Image.new("RGB", (2 * w, h))
    two.paste(im, (0, 0))
    two.paste(im, (w, 0))
    return two


def street_view(w, h, seed, smear, offset, strip=4200):
    x = int(offset) % strip
    return street_moving(strip, h, seed, smear).crop((x, 0, x + w, h))


@lru_cache(maxsize=None)
def crosshair(size, alpha):
    c = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(c)
    m, gap, ln, th, edge = size // 2, size // 9, size // 4, max(4, size // 22), 2
    strokes = [(m - gap - ln, m - th // 2, m - gap, m + th // 2), (m + gap, m - th // 2, m + gap + ln, m + th // 2),
               (m - th // 2, m - gap - ln, m + th // 2, m - gap), (m - th // 2, m + gap, m + th // 2, m + gap + ln)]
    dot = max(3, size // 30)
    for x0, y0, x1, y1 in strokes:
        d.rectangle((x0 - edge, y0 - edge, x1 + edge, y1 + edge), fill=(12, 14, 12, 230))
    d.ellipse((m - dot - edge, m - dot - edge, m + dot + edge, m + dot + edge), fill=(12, 14, 12, 230))
    for x0, y0, x1, y1 in strokes:
        d.rectangle((x0, y0, x1, y1), fill=(250, 252, 250, 255))
    d.ellipse((m - dot, m - dot, m + dot, m + dot), fill=(250, 252, 250, 255))
    if alpha < 255:
        c = c.filter(ImageFilter.GaussianBlur(1.2))
        c.putalpha(c.getchannel("A").point(lambda v: v * alpha // 255))
    return c


@lru_cache(maxsize=None)
def panel_mask(w, h, radius):
    m = Image.new("L", (w * 2, h * 2), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, w * 2 - 1, h * 2 - 1), radius=radius * 2, fill=255)
    return m.resize((w, h), Image.BILINEAR)


@lru_cache(maxsize=None)
def panel_edge(w, h, radius, lime):
    e = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(e).rounded_rectangle((0, 0, w - 1, h - 1), radius=radius, outline=(LIME if lime else EDGE) + (255,), width=3 if lime else 2)
    return e


def panel(canvas, picture, x, y, alpha, lime=False, radius=18):
    """A drawn picture as a rounded panel with the window's edge."""
    w, h = picture.size
    p = picture.convert("RGBA")
    p.putalpha(panel_mask(w, h, radius))
    put(canvas, p, x, y, alpha)
    put(canvas, panel_edge(w, h, radius, lime), x, y, alpha)


def heading(canvas, parts, y, k, alpha):
    sp = text_sprite(parts, "title", 84)
    put(canvas, sp, 960 - sp.width / 2, y + (1 - k) * 24, alpha)


# ---------------------------------------------------------------- scenes
def scene_hold_demo(canvas, t):
    # What Crosshair Hold is for, drawn: the same turning street twice. On the left frame generation moves the
    # crosshair with the picture in every frame it makes (gone from the middle, a faint copy on each side); on
    # the right it is held. Each frame is shown four times, so the film's 60 a second show it at any speed.
    t0, t1 = beat(8), beat(16)
    enter = out_cubic(at(t, t0, t0 + 0.3))
    a = enter * (1 - at(t, t1 - 0.2, t1))
    heading(canvas, [("Crosshair ", T1), ("Hold", LIME)], 104, enter, a)
    pw, ph = 800, 450
    n = int((t - t0) * FPS + 1e-6)
    made = (n // 4) % 2 == 1            # this frame is one frame generation made
    offset = (t - t0) * 700
    for i, held in enumerate((False, True)):
        k = out_cubic(at(t, t0 + 0.12 + 0.16 * i, t0 + 0.5 + 0.16 * i))
        if k <= 0.01:
            continue
        pic = street_view(pw, ph, 7, 3, offset).convert("RGBA")
        cx, cy, size = pw // 2, int(ph * 0.52), 132
        if held or not made:
            pic.alpha_composite(crosshair(size, 255), (cx - size // 2, cy - size // 2))
        else:
            g = crosshair(size, 118)
            for dx in (-92, 92):
                pic.alpha_composite(g, (cx + dx - size // 2, cy - size // 2))
        x, y = 140 + i * (pw + 40), 262 + (1 - k) * 30
        panel(canvas, pic, x, y, a * k, lime=held)
        put(canvas, chip("Crosshair Hold on" if held else "Frame generation", held), x + 22, y + 20, a * k)
    caption(canvas, t, t0 + 0.3, t1, "Your crosshair stays sharp while the camera moves", "Illustration, slowed down", y=806)


def scene_hold_where(canvas, t):
    b0, b1 = 16, 24
    row = (1180, 230, 396, 58)
    cam = cam_mix(cam_base(), cam_region(1166, 54, 426, 392, max_s=2.05), in_out_cubic(at(t, beat(17.4), beat(17.4) + 0.7)))
    enter = out_cubic(at(t, beat(b0), beat(b0) + 0.3))
    leave = at(t, beat(b1) - 0.18, beat(b1))
    window(canvas, "T-02-home-hud", cam, alpha=enter * (1 - leave), shadow=clamp(1.7 - cam[0]))
    lit = at(t, beat(19), beat(19) + 0.3) * (1 - at(t, beat(b1) - 0.2, beat(b1)))
    focus(canvas, cam, row, lit, radius=8, grow=5, blur=5.0, dim=0.36)
    highlight(canvas, cam, row, lit * 0.9, radius=8, grow=5)
    scrim(canvas, cam, enter * (1 - leave))
    caption(canvas, t, beat(b0) + 0.2, beat(19.6), "Where: quick setup, under HUD", "On the home page, beside the picture")
    caption(canvas, t, beat(19.6) + 0.1, beat(b1), "One switch", "Off until you turn it on. It says when it has found your crosshair")


def scene_fold(canvas, t):
    b0, b1 = 24, 36
    cam = cam_base()
    enter = out_cubic(at(t, beat(b0), beat(b0) + 0.3))
    a = enter * (1 - at(t, beat(b1) - 0.18, beat(b1)))
    k1 = in_out(at(t, beat(27), beat(27) + 0.3))
    k2 = in_out(at(t, beat(31), beat(31) + 0.3))
    if k2 < 0.999:
        window(canvas, "T-01-home", cam, alpha=a)
        if k1 > 0.01:
            window(canvas, "T-04-nav-folded", cam, alpha=a * k1, shadow=0)
    if k2 > 0.01:
        window(canvas, "T-06-both-folded", cam, alpha=a * k2, shadow=0)
    left, right = (13, 9, 30, 30), (1417, 9, 30, 30)
    highlight(canvas, cam, left, a * at(t, beat(25.4), beat(25.4) + 0.25) * (1 - at(t, beat(28.6), beat(29))), radius=7, grow=7)
    highlight(canvas, cam, right, a * at(t, beat(29.4), beat(29.4) + 0.25) * (1 - at(t, beat(32.6), beat(33))), radius=7, grow=7)
    caption(canvas, t, beat(b0) + 0.2, beat(29), "Both sides fold away", "One button each, at the ends of the title bar")
    caption(canvas, t, beat(29) + 0.1, beat(b1), "More room for the picture", "Navigation as icons, settings out of the way until you want them")


def scene_pro(canvas, t):
    b0, b1 = 36, 44
    s, X, Y = cam_base()
    k = 1.07                             # a little nearer, growing from the window's bottom right corner
    close = (s * k, X + V.SW * s - V.SW * s * k, Y + V.SH * s - V.SH * s * k)
    cam = cam_mix((s, X, Y), close, in_out_cubic(at(t, beat(37), beat(37) + 0.9)))
    enter = out_cubic(at(t, beat(b0), beat(b0) + 0.3))
    a = enter * (1 - at(t, beat(b1) - 0.18, beat(b1)))
    fold = in_out(at(t, beat(40), beat(40) + 0.3))
    window(canvas, "T-07-pro-all", cam, alpha=a)
    if fold > 0.01:
        window(canvas, "T-08-pro-folded", cam, alpha=a * fold, shadow=0)
    title = (1174, 272, 408, 34)
    highlight(canvas, cam, title, a * at(t, beat(38.6), beat(38.6) + 0.25) * (1 - at(t, beat(42.4), beat(42.8))), radius=8, grow=4)
    caption(canvas, t, beat(b0) + 0.2, beat(40), "Pro mode's categories fold", "Click a title in the All tab")
    caption(canvas, t, beat(40) + 0.1, beat(b1), "Keep open what you use", "PushFPS remembers it")


def scene_home_playing(canvas, t):
    b0, b1 = 44, 52
    picture = (528, 62, 632, 356)
    cam = cam_mix(cam_region(380, 40, 940, 600, max_s=1.5), cam_region(516, 50, 656, 562, max_s=1.9), in_out_cubic(at(t, beat(45), beat(45) + 0.9)))
    enter = out_cubic(at(t, beat(b0), beat(b0) + 0.3))
    leave = at(t, beat(b1) - 0.18, beat(b1))
    window(canvas, "T-11-home-playing", cam, alpha=enter * (1 - leave), shadow=0)
    lit = at(t, beat(46.4), beat(46.4) + 0.3) * (1 - at(t, beat(b1) - 0.2, beat(b1)))
    highlight(canvas, cam, picture, lit * 0.85, radius=10, grow=4)
    scrim(canvas, cam, enter * (1 - leave))
    caption(canvas, t, beat(b0) + 0.2, beat(48), "The picture plays on Home", "A capture card connects without leaving the page")
    caption(canvas, t, beat(48) + 0.1, beat(b1), "Tools right under it", "Or go straight to Pro mode: your choice in quick setup")


@lru_cache(maxsize=None)
def subtitle_box(w, original, translated):
    """The two lines of a live translation as the app draws them over a picture: original small, translation large."""
    h = 132
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w - 1, h - 1), radius=18, fill=(8, 10, 9, 214))
    if original:
        f = font("regular", 30)
        d.text(((w - f.getlength(original)) / 2, 18), original, font=f, fill=T2 + (255,))
    f = font("bold", 40)
    d.text(((w - f.getlength(translated)) / 2, 64), translated, font=f, fill=(255, 255, 255, 255))
    return im


def ring(size, share):
    """How much of the hold is left: a lime arc that empties."""
    im = Image.new("RGBA", (size * 2, size * 2), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.ellipse((8, 8, size * 2 - 9, size * 2 - 9), fill=(8, 10, 9, 214), outline=(60, 68, 62, 255), width=10)
    if share > 0.003:
        d.arc((8, 8, size * 2 - 9, size * 2 - 9), -90, -90 + 360 * share, fill=LIME + (255,), width=10)
    return im.resize((size, size), Image.LANCZOS)


def scene_subtitle(canvas, t):
    # Drawn: a line of live translation. The original ends, the translation stays for the hold and then goes.
    t0, t1 = beat(52), beat(60)
    enter = out_cubic(at(t, t0, t0 + 0.3))
    a = enter * (1 - at(t, t1 - 0.2, t1))
    heading(canvas, [("Subtitles ", T1), ("you can finish reading", LIME)], 96, enter, a)
    pw, ph = 1280, 500
    x, y = 320, 250 + (1 - enter) * 30
    panel(canvas, street_view(pw, ph, 3, 1, (t - t0) * 90), x, y, a)
    shown, ends = beat(53.0), beat(56.2)
    hold = 1.5
    orig = at(t, shown, shown + 0.2) * (1 - at(t, ends, ends + 0.18))
    trans = at(t, shown, shown + 0.2) * (1 - at(t, ends + hold, ends + hold + 0.2))
    both = subtitle_box(900, "The bridge is closed. Take the next exit.", "El puente está cerrado. Toma la siguiente salida.")
    alone = subtitle_box(900, "", "El puente está cerrado. Toma la siguiente salida.")
    bx, by = x + (pw - 900) / 2, y + ph - 132 - 34
    put(canvas, alone, bx, by, a * trans)
    put(canvas, both, bx, by, a * orig)
    left = 1 - at(t, ends, ends + hold)
    if t >= ends and trans > 0.01:
        sp = ring(86, left)
        put(canvas, sp, x + pw - 86 - 28, y + 26, a * trans)
        lab = words("%.1f s" % (left * hold), "semi", 26, T1)
        put(canvas, lab, x + pw - 43 - 28 - lab.width / 2, y + 26 + 43 - lab.height / 2, a * trans)
        put(canvas, chip("The original line has ended", False), x + 26, y + 26, a * trans * at(t, ends, ends + 0.25))
    caption(canvas, t, t0 + 0.3, t1, "Translated lines stay on screen", "1.5 seconds after the original ends, or as long as you set. Illustration", y=806)


def scene_check(canvas, t):
    b0, b1 = 60, 68
    tile = (739, 491, 201, 52)
    cam = cam_mix(cam_base(), cam_region(520, 430, 648, 190, max_s=1.9), in_out_cubic(at(t, beat(61), beat(61) + 0.8)))
    enter = out_cubic(at(t, beat(b0), beat(b0) + 0.3))
    leave = at(t, beat(b1) - 0.18, beat(b1))
    window(canvas, "T-01-home", cam, alpha=enter * (1 - leave), shadow=clamp(1.7 - cam[0]))
    lit = at(t, beat(62.2), beat(62.2) + 0.3) * (1 - at(t, beat(b1) - 0.2, beat(b1)))
    focus(canvas, cam, tile, lit, radius=10, grow=5, blur=5.0, dim=0.36)
    highlight(canvas, cam, tile, lit * 0.9, radius=10, grow=5)
    scrim(canvas, cam, enter * (1 - leave))
    caption(canvas, t, beat(b0) + 0.2, beat(64), "Check my setup", "Looks at the running picture and says what costs smoothness")
    caption(canvas, t, beat(64) + 0.1, beat(b1), "New: help for AMD and Intel cards", "Offers a lighter motion estimate when the card can't keep up")


def frame(t):
    canvas = V.background().copy()
    if t < beat(8):
        V.scene_intro(canvas, t)
    if beat(8) - 0.05 <= t < beat(16):
        scene_hold_demo(canvas, t)
    if beat(16) <= t < beat(24):
        scene_hold_where(canvas, t)
    if beat(24) <= t < beat(36):
        scene_fold(canvas, t)
    if beat(36) <= t < beat(44):
        scene_pro(canvas, t)
    if beat(44) <= t < beat(52):
        scene_home_playing(canvas, t)
    if beat(52) <= t < beat(60):
        scene_subtitle(canvas, t)
    if beat(60) <= t < beat(68):
        scene_check(canvas, t)
    if beat(68) <= t < beat(80):
        V.scene_also(canvas, t)
    if t >= beat(80):
        V.scene_outro(canvas, t)
    return canvas.convert("RGB")


# The 4.3 film's render loop and join, with this film's pictures and parts.
V.frame = frame
V.PARTS = [("intro", 0, 8), ("hold", 8, 24), ("fold", 24, 36), ("pro", 36, 52), ("more", 52, 68), ("also", 68, 80), ("outro", 80, None)]

if __name__ == "__main__":
    V.main()
