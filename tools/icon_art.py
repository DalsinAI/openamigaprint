#!/usr/bin/env python3
# Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT
"""OpenPrint's icon pictures in full colour, for OS 3.2-style icons.

    python3 tools/icon_art.py NAME OUT.rgba     NAME: printer viewer page pdf picture install

Each is drawn at four times its size and reduced (48 x 40, raw RGBA), then
made hard-edged: an icon has one see-through colour."""
import sys
from PIL import Image, ImageDraw, ImageFont

W, H, S = 48, 40, 4


def canvas():
    img = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def r(*v):
    return tuple(int(x * S) for x in v)


def shadow(d, box, radius=2):
    x0, y0, x1, y1 = box
    d.rounded_rectangle(r(x0 + 2, y0 + 2, x1 + 2, y1 + 2), radius=radius * S, fill=(40, 40, 60, 110))


def landscape(d, x0, y0, x1, y1):
    d.rectangle(r(x0, y0, x1, y1), fill=(120, 180, 240, 255))
    d.ellipse(r(x1 - 9, y0 + 2, x1 - 3, y0 + 8), fill=(255, 215, 60, 255))
    d.polygon([r(x0, y1)[0:2], r(x0 + (x1 - x0) * 0.45, y0 + (y1 - y0) * 0.45), r(x1, y1)], fill=(70, 160, 80, 255))
    d.polygon([r(x0 + (x1 - x0) * 0.35, y1), r(x0 + (x1 - x0) * 0.7, y0 + (y1 - y0) * 0.55), r(x1, y1)], fill=(40, 120, 60, 255))


def page(d, x0=10, y0=3, x1=38, y1=37, fold=7):
    shadow(d, (x0, y0, x1, y1), 1)
    d.polygon([r(x0, y0), r(x1 - fold, y0), r(x1, y0 + fold), r(x1, y1), r(x0, y1)], fill=(255, 255, 255, 255), outline=(48, 48, 64, 255))
    d.polygon([r(x1 - fold, y0), r(x1 - fold, y0 + fold), r(x1, y0 + fold)], fill=(210, 210, 220, 255), outline=(48, 48, 64, 255))


def draw(name):
    img, d = canvas()
    if name == "printer":
        shadow(d, (4, 14, 44, 33))
        d.rectangle(r(13, 3, 35, 15), fill=(255, 255, 255, 255), outline=(48, 48, 64, 255), width=S // 2)      # the paper going in
        for y in (6, 9, 12):
            d.line(r(16, y, 32, y), fill=(150, 150, 170, 255), width=S // 2)
        d.rounded_rectangle(r(4, 14, 44, 32), radius=3 * S, fill=(196, 200, 212, 255), outline=(48, 48, 64, 255), width=S)
        d.rectangle(r(6, 16, 42, 19), fill=(226, 230, 240, 255))
        d.rectangle(r(12, 24, 36, 27), fill=(60, 64, 80, 255))                                               # the slot
        d.rectangle(r(13, 26, 35, 37), fill=(255, 255, 255, 255), outline=(48, 48, 64, 255), width=S // 2)    # the page coming out
        d.ellipse(r(37, 20, 40, 23), fill=(60, 200, 90, 255))                                                # ready light
        d.rectangle(r(6, 20, 10, 22), fill=(40, 90, 200, 255))
    elif name == "viewer":
        shadow(d, (4, 5, 40, 33))
        d.rectangle(r(4, 5, 40, 33), fill=(250, 246, 232, 255), outline=(48, 48, 64, 255), width=S)
        landscape(d, 7, 8, 37, 30)
        d.ellipse(r(25, 18, 39, 32), fill=(200, 230, 255, 140), outline=(40, 40, 60, 255), width=S)          # the magnifier
        d.line(r(37, 30, 45, 38), fill=(120, 70, 30, 255), width=3 * S)
    elif name == "page":
        page(d)
        for i, y in enumerate((12, 16, 20, 24, 28, 32)):
            d.line(r(14, y, 34 - (6 if i == 5 else 0), y), fill=(90, 90, 110, 255), width=S)
        d.rectangle(r(14, 6, 26, 9), fill=(40, 90, 200, 255))
    elif name == "pdf":
        page(d)
        for y in (24, 28, 32):
            d.line(r(14, y, 34, y), fill=(120, 120, 140, 255), width=S)
        d.rectangle(r(10, 10, 32, 19), fill=(200, 40, 40, 255))
        try:
            font = ImageFont.truetype("DejaVuSans-Bold.ttf", 7 * S)
        except OSError:
            font = ImageFont.load_default()
        d.text(r(12, 10.5), "PDF", fill=(255, 255, 255, 255), font=font)
    elif name == "picture":
        shadow(d, (5, 6, 43, 34))
        d.rectangle(r(5, 6, 43, 34), fill=(250, 246, 232, 255), outline=(48, 48, 64, 255), width=S)
        landscape(d, 8, 9, 40, 31)
    elif name == "install":
        shadow(d, (6, 14, 38, 36))
        d.polygon([r(6, 14), r(38, 14), r(38, 36), r(6, 36)], fill=(200, 150, 90, 255), outline=(80, 50, 20, 255))
        d.polygon([r(6, 14), r(12, 8), r(44, 8), r(38, 14)], fill=(225, 180, 120, 255), outline=(80, 50, 20, 255))
        d.polygon([r(38, 14), r(44, 8), r(44, 30), r(38, 36)], fill=(170, 120, 70, 255), outline=(80, 50, 20, 255))
        d.rectangle(r(18, 2, 26, 14), fill=(60, 180, 80, 255), outline=(20, 90, 40, 255))                     # the arrow, going in
        d.polygon([r(13, 13), r(31, 13), r(22, 24)], fill=(60, 180, 80, 255), outline=(20, 90, 40, 255))
    else:
        raise SystemExit(f"unknown picture {name}")
    img = img.resize((W, H), Image.LANCZOS)
    px = img.load()
    for y in range(H):
        for x in range(W):
            rr, g, b, a = px[x, y]
            px[x, y] = (rr, g, b, 255) if a >= 128 else (0, 0, 0, 0)
    return img.tobytes()


if __name__ == "__main__":
    open(sys.argv[2], "wb").write(draw(sys.argv[1]))
