#!/usr/bin/env python3
"""The Remote Play setup graphics: PS5, Xbox, and what to check when it does not work.

PushFPS brand (graphite, the logo's green, the classic logo, Barlow). Everything in them is drawn here:
no screenshot, no console maker's mark, nobody's account name. The steps are the ones in the README's
"Remote Play setup" and the website's #remote-play: change the three together.

    python3 scripts/brand/make_remote_play_guide.py            # writes assets/guides and website/assets/guides
    python3 scripts/brand/make_remote_play_guide.py <folder>   # writes the PNGs there only (for a post)
"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
S = 2                                    # drawn at twice the size, then reduced
W, H = 1200 * S, 1500 * S                # 4:5, the tallest picture a post shows whole
GREEN = (141, 242, 55)
INK = (236, 240, 236)
SOFT = (168, 178, 170)
DIM = (104, 114, 106)
PANEL = (22, 26, 23)
LINE = (52, 60, 54)
DARK = (10, 16, 8)
FONTS = ROOT / "website/fonts"


def font(name, size):
    return ImageFont.truetype(str(FONTS / f"{name}.woff2"), int(size * S))


HEAD = font("BarlowSemiCondensed-ExtraBoldItalic", 88)
SUB = font("Barlow-Medium", 31)
GROUP = font("BarlowSemiCondensed-SemiBold", 33)
TITLE = font("Barlow-SemiBold", 35)
BODY = font("Barlow-Regular", 27)
BOLD = font("Barlow-SemiBold", 27)
CHIP = font("Barlow-Medium", 24)
NUM = font("BarlowSemiCondensed-ExtraBoldItalic", 40)
CODE = font("BarlowSemiCondensed-SemiBold", 50)
TINY = font("Barlow-Medium", 22)
FOOT = font("Barlow-Medium", 24)
PILL = font("BarlowSemiCondensed-SemiBold", 28)


def px(v):
    return int(round(v * S))


def backdrop():
    img = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(img)
    for y in range(H):
        t = y / (H - 1)
        d.line((0, y, W, y), fill=(int(14 + 5 * t), int(17 + 5 * t), int(14 + 4 * t)))
    glow = Image.new("L", (W, H), 0)
    ImageDraw.Draw(glow).ellipse((px(560), px(-260), px(1500), px(520)), fill=70)
    glow = glow.filter(ImageFilter.GaussianBlur(px(170)))
    return Image.composite(Image.new("RGB", (W, H), GREEN), img, glow.point(lambda v: int(v * 0.40))).convert("RGBA")


def spans(text):
    """'press *Search* now' -> [('press ', False), ('Search', True), (' now', False)]"""
    out, bold = [], False
    for part in text.split("*"):
        if part:
            out.append((part, bold))
        bold = not bold
    return out


def wrap(d, text, width):
    """Lines of (word, bold) lists that fit the width; a word is never split."""
    lines, line, used = [], [], 0.0
    for chunk, bold in spans(text):
        f = BOLD if bold else BODY
        for i, word in enumerate(chunk.split(" ")):
            if i and line and not line[-1][0].endswith(" "):
                line[-1] = (line[-1][0] + " ", line[-1][1])
                used += d.textlength(" ", font=BOLD if line[-1][1] else BODY)
            if not word:
                continue
            w = d.textlength(word, font=f)
            if line and used + w > width:
                lines.append(line)
                line, used = [], 0.0
            line.append((word, bold))
            used += w
    if line:
        lines.append(line)
    return lines


def draw_lines(d, x, y, lines, step):
    for line in lines:
        cx = x
        for word, bold in line:
            f = BOLD if bold else BODY
            d.text((cx, y), word, font=f, fill=INK if bold else SOFT, anchor="ls")
            cx += d.textlength(word, font=f)
        y += step
    return y


def chips_layout(d, path, width):
    """Rows of chips for a settings path; the last one is what gets pressed."""
    rows, row, used = [], [], 0.0
    arrow = d.textlength("›", font=CHIP) + px(18)
    for i, name in enumerate(path):
        w = d.textlength(name, font=CHIP) + px(30)
        need = w + (arrow if row else 0)
        if row and used + need > width:
            rows.append(row)
            row, used, need = [], 0.0, w
        row.append((name, w, i == len(path) - 1))
        used += need
    if row:
        rows.append(row)
    return rows


def draw_chips(d, x, y, rows):
    h = px(42)
    for row in rows:
        cx = x
        for k, (name, w, last) in enumerate(row):
            if k:
                d.text((cx + px(9), y + h / 2), "›", font=CHIP, fill=DIM, anchor="lm")
                cx += d.textlength("›", font=CHIP) + px(18)
            d.rounded_rectangle((cx, y, cx + w, y + h), px(21), outline=GREEN if last else LINE, width=px(2),
                                fill=(30, 44, 26) if last else (28, 33, 29))
            d.text((cx + w / 2, y + h / 2), name, font=CHIP, fill=GREEN if last else SOFT, anchor="mm")
            cx += w
        y += h + px(10)
    return y - px(10)


def shadowed(img, box, radius):
    shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    x0, y0, x1, y1 = box
    ImageDraw.Draw(shadow).rounded_rectangle((x0, y0 + px(8), x1, y1 + px(8)), radius, fill=(0, 0, 0, 120))
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(px(14))))


def base(tag, head, sub):
    img = backdrop()
    d = ImageDraw.Draw(img)
    logo = Image.open(ROOT / "assets/pushfps-logo.png").convert("RGBA")
    lh = px(66)
    logo = logo.resize((int(logo.width * lh / logo.height), lh), Image.LANCZOS)
    img.alpha_composite(logo, (px(60), px(46)))
    tw = d.textlength(tag, font=PILL)
    d.rounded_rectangle((W - px(60) - tw - px(36), px(56), W - px(60), px(102)), px(23), outline=GREEN, width=px(2))
    d.text((W - px(60) - px(18), px(79)), tag, font=PILL, fill=GREEN, anchor="rm")
    d.text((px(60), px(222)), head, font=HEAD, fill=INK, anchor="ls")
    y = px(274)
    for line in sub:
        d.text((px(62), y), line, font=SUB, fill=SOFT, anchor="ls")
        y += px(42)
    d.line((px(60), H - px(78), W - px(60), H - px(78)), fill=(44, 50, 45), width=px(1))
    d.text((px(60), H - px(36)), "Full guide: dosukasol.github.io/PushFPS", font=FOOT, fill=SOFT, anchor="ls")
    d.text((W - px(60), H - px(36)), "Free and open source", font=FOOT, fill=SOFT, anchor="rs")
    return img, d, y


def group(d, y, text):
    d.text((px(62), y + px(30)), text, font=GROUP, fill=GREEN, anchor="ls")
    x = px(62) + d.textlength(text, font=GROUP) + px(18)
    d.line((x, y + px(20), W - px(60), y + px(20)), fill=LINE, width=px(2))
    return y + px(50)


def step(img, y, number, title, text=None, path=None, code=None, code_note=None, gap=12):
    """One step as a card: the number, what to do, where it is, and what it looks like."""
    d = ImageDraw.Draw(img)
    x0, x1 = px(60), W - px(60)
    left = x0 + px(104)
    box_w = px(250) if code else 0
    width = x1 - left - px(28) - (box_w + px(22) if code else 0)
    lines = wrap(d, text, width) if text else []
    rows = chips_layout(d, path, width) if path else []
    height = px(26) + px(40) + (px(8) + len(lines) * px(36) if lines else 0)
    if rows:
        height += px(14) + len(rows) * px(42) + (len(rows) - 1) * px(10)
    height += px(24)
    if code:
        height = max(height, px(26 + 104 + 26))
    shadowed(img, (x0, y, x1, y + height), px(22))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((x0, y, x1, y + height), px(22), fill=PANEL, outline=LINE, width=px(2))
    cy = y + px(26) + px(30)
    d.ellipse((x0 + px(24), cy - px(30), x0 + px(84), cy + px(30)), fill=GREEN)
    d.text((x0 + px(53), cy + px(1)), str(number), font=NUM, fill=DARK, anchor="mm")
    ty = y + px(26) + px(34)
    d.text((left, ty), title, font=TITLE, fill=INK, anchor="ls")
    ty += px(8)
    if lines:
        ty = draw_lines(d, left, ty + px(34), lines, px(36)) - px(36) + px(6)
    if rows:
        draw_chips(d, left, ty + px(14), rows)
    if code:
        bx1 = x1 - px(26)
        bx0 = bx1 - box_w
        by0 = y + (height - px(104)) // 2
        d.rounded_rectangle((bx0, by0, bx1, by0 + px(104)), px(16), fill=(14, 18, 15), outline=GREEN, width=px(2))
        d.text(((bx0 + bx1) / 2, by0 + px(44)), code, font=CODE, fill=INK, anchor="mm")
        d.text(((bx0 + bx1) / 2, by0 + px(82)), code_note or "", font=TINY, fill=SOFT, anchor="mm")
    return y + height + px(gap)


def checks(img, y, title, items, gap=16):
    """A heading and its list: what to check, shortest first."""
    d = ImageDraw.Draw(img)
    x0, x1 = px(60), W - px(60)
    width = x1 - x0 - px(96)
    laid = [wrap(d, item, width) for item in items]
    height = px(74) + sum(len(lines) * px(36) + px(14) for lines in laid) + px(12)
    shadowed(img, (x0, y, x1, y + height), px(22))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((x0, y, x1, y + height), px(22), fill=PANEL, outline=LINE, width=px(2))
    d.text((x0 + px(30), y + px(52)), title, font=TITLE, fill=GREEN, anchor="ls")
    ty = y + px(74) + px(30)
    for lines in laid:
        d.ellipse((x0 + px(34), ty - px(17), x0 + px(46), ty - px(5)), fill=GREEN)
        ty = draw_lines(d, x0 + px(66), ty, lines, px(36)) + px(14)
    return y + height + px(gap)


def note(img, y, text):
    """A plain line under the steps: something to know, not something to do."""
    d = ImageDraw.Draw(img)
    lines = wrap(d, text, W - px(124))
    return draw_lines(d, px(62), y + px(34), lines, px(36))


def save(img, name, folder=None):
    small = img.convert("RGB").resize((W // S, H // S), Image.LANCZOS)
    if folder:
        out = Path(folder) / f"{name}.png"
        small.save(out, optimize=True)
        print("saved", out)
        return
    out = ROOT / "assets/guides" / f"{name}.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    small.save(out, optimize=True)
    web = ROOT / "website/assets/guides" / f"{name}.webp"
    web.parent.mkdir(parents=True, exist_ok=True)
    small.save(web, "WEBP", quality=84, method=6)
    print("saved", out, "and", web)


def ps5(folder):
    img, d, y = base("Setup guide", "PS5 Remote Play", ["No capture card needed. PS5 and PC on the same home network."])
    y = group(d, y + px(6), "On your PS5")
    y = step(img, y, 1, "Turn Remote Play on", path=["Settings", "System", "Remote Play", "Enable Remote Play"])
    y = step(img, y, 2, "Get the pairing number", "Leave it on screen. It is good for 5 minutes.",
             path=["Settings", "System", "Remote Play", "Pair Device"], code="1234 5678", code_note="8 digits")
    y = group(ImageDraw.Draw(img), y + px(6), "In PushFPS")
    y = step(img, y, 3, "Open PS5 streaming, press Search", "It finds your PS5 and fills in its address.")
    y = step(img, y, 4, "Press Get Account ID", "Look up your PSN name on the page that opens and copy the *Account ID* into PushFPS. Your PSN name itself does not work.")
    y = step(img, y, 5, "Type the 8 digits, press Pair and save", "Type them without the space.")
    y = step(img, y, 6, "Press Connect", "Controller: plug it into the *PC* with a USB cable, or pair it with the PC's Bluetooth.")
    save(img, "remote-play-ps5", folder)
    return y


def xbox(folder):
    img, d, y = base("Setup guide", "Xbox Remote Play", ["No capture card needed. Xbox and PC on the same home network."])
    y = group(d, y + px(14), "On your Xbox")
    y = step(img, y, 1, "Turn remote features on", path=["Settings", "Devices & connections", "Remote features", "Enable remote features"], gap=20)
    y = step(img, y, 2, "Set the power option to Sleep", path=["Settings", "General", "Power options", "Sleep"], gap=20)
    y = group(ImageDraw.Draw(img), y + px(14), "In PushFPS")
    y = step(img, y, 3, "Open Xbox streaming, press Sign in", "PushFPS shows a code. Enter it at *microsoft.com/link* and sign in with the account your Xbox uses.",
             code="H7K2M9QD", code_note="your code", gap=20)
    y = step(img, y, 4, "Pick your console, press Start streaming", "Not in the list? Press *Refresh*. The Xbox has to be signed in with the same account.", gap=20)
    y = step(img, y, 5, "Use this PC's controller", "Plug it into the *PC*: USB cable, wireless adapter or Bluetooth. Leave the *Controller* switch on.", gap=20)
    y = note(img, y, "Xbox streaming is unofficial and experimental: it can stop working when Microsoft changes its service.")
    save(img, "remote-play-xbox", folder)
    return y


def help_card(folder):
    img, d, y = base("Setup guide", "Remote Play not working?", ["What to check, quickest first."])
    y += px(4)
    y = checks(img, y, "The PS5 will not pair", [
        "*Account ID* is the number from *Get Account ID*, not your PSN name.",
        "The 8 digits: no space, and a new number once 5 minutes have passed.",
        "No address? Press *Search*, or type the PS5's IP address.",
        "PS5 and PC on the same network. No guest Wi-Fi, no VPN."])
    y = checks(img, y, "The controller does nothing", [
        "Connect it to the *PC*, not to the console.",
        "Keep PushFPS in front: click its window once.",
        "PS5: *View only* off. Xbox: *Controller* on."])
    y = checks(img, y, "The Xbox is not in the list", [
        "Sign in on the console with the same Microsoft account.",
        "Remote features on, power option *Sleep*, then press *Refresh*."])
    y = checks(img, y, "It stutters, drops out or closes", [
        "Use a network cable for the console, and for the PC if you can.",
        "PS5: lower *Requested bitrate*, or set *Decoding* to CPU software.",
        "Xbox: turn on *Software video decoding*.",
        "Still stuck? *Settings › About › Report a problem*."])
    save(img, "remote-play-help", folder)
    return y


if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else None
    if target:
        Path(target).mkdir(parents=True, exist_ok=True)
    for make in (ps5, xbox, help_card):
        bottom = make(target)
        print(make.__name__, "ends at", round(bottom / S), "of", (H - px(78)) // S)
