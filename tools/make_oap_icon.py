#!/usr/bin/env python3
# Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT
"""Generate the classic Workbench icon for DEVS:Printers/OpenPrint."""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

WIDTH = 48
HEIGHT = 32
WBPROJECT = 4
NO_ICON_POSITION = -0x80000000

# Four-colour Workbench 2.x/3.x pens: 0 background, 1 black, 2 white, 3 accent.
FONT = {
    "P": ("110", "101", "110", "100", "100"),
    "D": ("110", "101", "101", "101", "110"),
    "F": ("111", "100", "110", "100", "100"),
}


def artwork(selected: bool) -> list[list[int]]:
    px = [[0 for _ in range(WIDTH)] for _ in range(HEIGHT)]

    def rect(x0: int, y0: int, x1: int, y1: int, edge: int, fill: int) -> None:
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                px[y][x] = fill
        for x in range(x0, x1 + 1):
            px[y0][x] = edge
            px[y1][x] = edge
        for y in range(y0, y1 + 1):
            px[y][x0] = edge
            px[y][x1] = edge

    paper = 3 if selected else 2
    body = 2 if selected else 3

    # Sheet entering the printer.
    rect(15, 1, 32, 14, 1, paper)
    px[1][29] = px[1][30] = px[1][31] = 1
    px[2][30] = 1
    px[2][31] = paper
    px[3][31] = 1

    # Tiny "PDF" mark on the sheet.
    x = 18
    for ch in "PDF":
        for yy, row in enumerate(FONT[ch]):
            for xx, bit in enumerate(row):
                if bit == "1":
                    px[5 + yy][x + xx] = 1 if selected else 3
        x += 4

    # Printer body, input slot and ready light.
    rect(7, 12, 40, 25, 1, body)
    rect(11, 14, 36, 18, 1, 0)
    for x in range(13, 35):
        px[16][x] = paper
    rect(35, 14, 38, 17, 1, paper)

    # Printed sheet emerging from the front.
    rect(13, 21, 34, 30, 1, paper)
    for y in (24, 26, 28):
        for x in range(16, 32):
            px[y][x] = 1 if selected else 3
    for x in range(9, 14):
        px[26][x] = 1
    for x in range(34, 39):
        px[26][x] = 1
    return px


def bitplanes(px: list[list[int]]) -> bytes:
    padded_width = (WIDTH + 15) & ~15
    plane_bytes = bytearray()
    for plane in range(2):
        for y in range(HEIGHT):
            for x0 in range(0, padded_width, 8):
                value = 0
                for bit in range(8):
                    x = x0 + bit
                    colour = px[y][x] if x < WIDTH else 0
                    if colour & (1 << plane):
                        value |= 1 << (7 - bit)
                plane_bytes.append(value)
    return bytes(plane_bytes)


def image_record(px: list[list[int]]) -> bytes:
    data = bitplanes(px)
    # struct Image: LeftEdge, TopEdge, Width, Height, Depth, ImageData,
    # PlanePick, PlaneOnOff, NextImage. Pointer values on disk are presence flags.
    header = struct.pack(
        ">hhhhhIBBI",
        0,
        0,
        WIDTH,
        HEIGHT,
        2,
        0,
        0x03,
        0x00,
        0,
    )
    return header + data


def build_icon() -> bytes:
    parts: list[bytes] = []
    parts.append(struct.pack(">HH", 0xE310, 1))

    # 44-byte Gadget. Both image pointers are non-zero presence flags.
    parts.append(
        struct.pack(
            ">IhhhhHHHIIIiIHI",
            0,
            0,
            0,
            WIDTH,
            HEIGHT,
            0x0006,  # GFLG_GADGIMAGE | GFLG_GADGHIMAGE
            0x0003,  # GACT_RELVERIFY | GACT_IMMEDIATE
            0x0001,  # GTYP_BOOLGADGET
            1,
            1,
            0,
            0,
            0,
            0,
            1,  # Workbench disk-object revision
        )
    )

    parts.append(struct.pack(">BB", WBPROJECT, 0))
    parts.append(struct.pack(">II", 0, 0))  # no DefaultTool or ToolTypes
    parts.append(struct.pack(">ii", NO_ICON_POSITION, NO_ICON_POSITION))
    parts.append(struct.pack(">II", 0, 0))  # no DrawerData or ToolWindow
    parts.append(struct.pack(">i", 4096))
    parts.append(image_record(artwork(False)))
    parts.append(image_record(artwork(True)))
    raw = b"".join(parts)

    expected = 78 + 2 * (20 + (WIDTH * HEIGHT * 2 // 8))
    if len(raw) != expected:
        raise RuntimeError(f"unexpected icon size {len(raw)} (expected {expected})")
    if raw[:4] != b"\xe3\x10\x00\x01":
        raise RuntimeError("invalid DiskObject header")
    if raw[0x30] != WBPROJECT:
        raise RuntimeError("invalid Workbench icon type")
    return raw


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", nargs="?", default="build/amigaos3/OpenPrint.info")
    args = parser.parse_args()
    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    raw = build_icon()
    out.write_bytes(raw)
    print(f"OpenPrint Workbench icon: {out} ({len(raw)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
