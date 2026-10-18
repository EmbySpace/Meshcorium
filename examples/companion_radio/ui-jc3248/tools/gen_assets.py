#!/usr/bin/env python3
"""
Generates ui_assets.h for the JC3248W535 UI: anti-aliased (4 bit alpha)
fonts and icons, stored in flash.

  python gen_assets.py [--montserrat DIR] [--fontawesome TTF] [--out FILE]

Fonts : Montserrat (SIL Open Font License 1.1), Latin + Cyrillic
Icons : Font Awesome 4.7 (SIL Open Font License 1.1)
Both licences allow embedding the rendered glyphs in firmware.

Needs Pillow. The default paths point to copies that come with SquareLine
Studio and the Arduino IDE on Windows; pass your own paths elsewhere.
"""
import argparse
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))

DEFAULT_MONTSERRAT = r"C:/Program Files/SquareLine Studio 1.6.2/examples/SmartWatch_392x392/assets/fonts"
DEFAULT_FA = os.path.expandvars(r"%LOCALAPPDATA%/Programs/Arduino IDE/resources/app/lib/frontend/1e59d2330b4c6deb84b3.ttf")

# (C name, Montserrat weight file, pixel size)
FONTS = [
    ("UI_FONT_SMALL",  "Montserrat-Medium.ttf",   15),
    ("UI_FONT_BODY",   "Montserrat-Medium.ttf",   18),
    ("UI_FONT_BODY_B", "Montserrat-SemiBold.ttf", 18),
    ("UI_FONT_TITLE",  "Montserrat-SemiBold.ttf", 22),
    ("UI_FONT_VALUE",  "Montserrat-Bold.ttf",     28),
    ("UI_FONT_LABEL",  "Montserrat-SemiBold.ttf", 13),   # small tracked caps labels
]

# big numerals only (clock, percent, values) - a small subset keeps flash down
DIGITS = [ord(c) for c in "0123456789:%.,-+/ V"] + [0x2013, 0x2014]
LETTERS = [ord(c) for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .-|"]
LOGO_FONTS = [
    ("UI_FONT_LOGO", "Montserrat-Light.ttf", 40),    # firmware name on the boot screen
]
HUGE_FONTS = [
    ("UI_FONT_HUGE_B", "Montserrat-Bold.ttf",  72),
    ("UI_FONT_NUM_B",  "Montserrat-Bold.ttf",  52),
]

CHARSET = (list(range(0x20, 0x7F)) +
           [0xA0, 0xAB, 0xB0, 0xB1, 0xB7, 0xBB, 0x401, 0x451] +
           list(range(0x410, 0x450)) +
           [0x2013, 0x2014, 0x2026, 0x2116, 0x2248, 0x2022])

# Font Awesome 4.7 code points
ICONS = {
    "WIFI": 0xf1eb, "BLUETOOTH": 0xf294, "USB": 0xf287, "USERS": 0xf0c0, "CLOCK": 0xf017,
    "BOLT": 0xf0e7, "HOURGLASS": 0xf252, "SIGNAL": 0xf012, "GLOBE": 0xf0ac, "COG": 0xf013,
    "SLIDERS": 0xf1de, "TACHOMETER": 0xf0e4, "PODCAST": 0xf2ce, "SUN": 0xf185, "MOON": 0xf186,
    "MICROCHIP": 0xf2db, "PLUG": 0xf1e6, "MINUS": 0xf068, "PLUS": 0xf067, "CHECK": 0xf00c,
    "HEARTBEAT": 0xf21e, "BAR_CHART": 0xf080, "COMMENTS": 0xf086, "LOCK": 0xf023,
    "INFO": 0xf05a, "ENVELOPE": 0xf0e0, "CALENDAR": 0xf073, "POWER": 0xf011, "RSS": 0xf09e,
    "HASHTAG": 0xf292, "CUBE": 0xf1b2, "BELL": 0xf0f3, "WRENCH": 0xf0ad, "KEY": 0xf084,
    "COMPASS": 0xf14e, "TH_LARGE": 0xf009, "HDD": 0xf0a0, "REFRESH": 0xf021, "WARNING": 0xf071,
    "QRCODE": 0xf029, "CHEVRON_LEFT": 0xf053, "CHEVRON_RIGHT": 0xf054, "PAPER_PLANE": 0xf1d8, "ARROW_UP": 0xf062,
    "BACKSPACE": 0xf177, "USER": 0xf007, "SERVER": 0xf233, "COMMENT": 0xf075,
}
ICON_SIZES = [("UI_ICON_S", 18), ("UI_ICON_M", 24), ("UI_ICON_L", 44)]


def render_glyph(font, cp):
    ch = chr(cp)
    adv = int(round(font.getlength(ch)))
    bbox = font.getbbox(ch, anchor="ls")     # relative to pen position on the baseline
    x0, y0, x1, y1 = bbox
    w, h = max(0, x1 - x0), max(0, y1 - y0)
    if w == 0 or h == 0:
        return adv, 0, 0, 0, 0, b""
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
    px = img.load()
    data = bytearray()
    for y in range(h):
        row = [(px[x, y] * 15 + 127) // 255 for x in range(w)]
        if len(row) % 2:
            row.append(0)
        for i in range(0, len(row), 2):
            data.append((row[i] << 4) | row[i + 1])
    return adv, w, h, x0, y0, bytes(data)


def has_glyph(font, cp, notdef_mask):
    m = font.getmask(chr(cp))
    return cp == 0x20 or cp == 0xA0 or (m.getbbox() is not None and bytes(m) != notdef_mask)


def emit_font(out, cname, font, codepoints):
    notdef = bytes(font.getmask(chr(0xFFFF)))
    ascent, descent = font.getmetrics()
    glyphs, blob = [], bytearray()
    for cp in sorted(set(codepoints)):      # sorted: the firmware binary-searches glyphs
        if not has_glyph(font, cp, notdef):
            continue
        adv, w, h, x0, y0, data = render_glyph(font, cp)
        glyphs.append((cp, w, h, x0, y0, adv, len(blob)))
        blob += data
    sym = cname.lower()
    out.append(f"// {font.getname()[0]} {font.getname()[1]} {font.size}px, {len(glyphs)} glyphs, {len(blob)} bytes")
    out.append(f"static const uint8_t {sym}_bits[] = {{")
    for i in range(0, len(blob), 24):
        out.append("  " + ",".join(f"0x{b:02x}" for b in blob[i:i + 24]) + ",")
    out.append("};")
    out.append(f"static const UIGlyph {sym}_glyphs[] = {{")
    for g in glyphs:
        out.append("  {0x%04x,%d,%d,%d,%d,%d,%d}," % g)
    out.append("};")
    out.append(f"static const UIFont {cname} = {{ {sym}_glyphs, {len(glyphs)}, {sym}_bits, "
               f"{ascent + descent}, {ascent}, {descent} }};")
    out.append("")
    return len(blob) + len(glyphs) * 12


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--montserrat", default=DEFAULT_MONTSERRAT)
    ap.add_argument("--fontawesome", default=DEFAULT_FA)
    ap.add_argument("--out", default=os.path.join(HERE, "..", "ui_assets.h"))
    a = ap.parse_args()

    out = [
        "// GENERATED by tools/gen_assets.py - do not edit.",
        "// Fonts: Montserrat (SIL OFL 1.1). Icons: Font Awesome 4.7 (SIL OFL 1.1).",
        "#pragma once",
        '#include "UIGfx.h"',
        "",
        "enum UIIcon : uint16_t {",
    ]
    for name, cp in ICONS.items():
        out.append(f"  ICON_{name} = 0x{cp:04x},")
    out.append("};")
    out.append("")

    total = 0
    for cname, file, size in FONTS:
        font = ImageFont.truetype(os.path.join(a.montserrat, file), size)
        total += emit_font(out, cname, font, CHARSET)
    for cname, file, size in HUGE_FONTS:
        font = ImageFont.truetype(os.path.join(a.montserrat, file), size)
        total += emit_font(out, cname, font, DIGITS)
    for cname, file, size in LOGO_FONTS:
        font = ImageFont.truetype(os.path.join(a.montserrat, file), size)
        total += emit_font(out, cname, font, LETTERS)

    fa_path = a.fontawesome
    for cname, size in ICON_SIZES:
        font = ImageFont.truetype(fa_path, size)
        total += emit_font(out, cname, font, sorted(ICONS.values()))

    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote {a.out}: ~{total // 1024} KB of flash")


if __name__ == "__main__":
    main()
