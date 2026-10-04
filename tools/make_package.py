#!/usr/bin/env python3
"""OpenPrint as an Amiga package: a drawer to run Installer from, and
the same as an LhA archive.

    python3 tools/make_package.py [--version 0.3] [--lha-module DIR] [--out build/package]

Dale, 4 October 2026: "it needs an amiga installer, or something like it".
The drawer holds the Installer script (package/Install-OpenPrint) with its
icon, the ReadMe, and what the script installs, laid out as it goes:
  C/          the five commands
  Devs/       oapspool.device, Printers/OpenPrint
  WBStartup/  the spooler
  Drawer/     OpenView, Printers and Queue, Picture.png, TestPage.pdf
The programs come from build/amigaos3 (run build-amiga.sh, build-browser.sh
and build-viewer.sh first). The icons are classic four-colour icons, made
here, so the package looks the same on every Workbench from 2.04 on.
--lha-module names the folder holding AmigaChrome's lha_archive.py; without
it only the drawer is made.
"""
from __future__ import annotations

import argparse
import math
import shutil
import struct
import sys
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "amigaos3"
NO_POSITION = -0x80000000
WBDISK, WBDRAWER, WBTOOL, WBPROJECT = 1, 2, 3, 4

# ---------------------------------------------------------------- icons
# Pictures as text, one character a pixel in the Workbench 2 pens:
# '.' grey (the background), '#' black, 'w' white, 'b' blue.
PENS = {".": 0, "#": 1, "w": 2, "b": 3}

ART = {
    "printer": """
.............######################.............
.............#wwwwwwwwwwwwwwwwwwww#.............
.............#w##########wwwwwwwww#.............
.............#wwwwwwwwwwwwwwwwwwww#.............
.............#w#############wwwwww#.............
....##########wwwwwwwwwwwwwwwwwwww##########....
...#bbbbbbbbbb######################bbbbbbbbb#...
..#bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb#..
..#bbbwwbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb##b##bb#..
..#bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb#..
..#bbbbbb##################################bbb#..
..#bbbbbb#wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww#bbb#..
..#bbbbbb#w#####################wwwwwwwww#bbb#..
...#######wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww####...
.........#w###############wwwwwwwwwwwwwww#......
.........#wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww#......
.........##################################......
""",
    "viewer": """
..############################################..
..#wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww#..
..#w##########################################..
..#w#bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb#..
..#w#bbbbbbbbbbbbbbbbbbbbbbbbbbbbbwwwbbbbbbbb#..
..#w#bbbbbbbbbbbbbbbbbbbbbbbbbbbbwwwwwbbbbbbb#..
..#w#bbbbbbbbbbbbbbbbbbbbbbbbbbbbbwwwbbbbbbbb#..
..#w#bbbbbbbbbbbb#bbbbbbbbbbbbbbbbbbbbbbbbbbb#..
..#w#bbbbbbbbbbb###bbbbbbbbbbbbbbbbbbbbbbbbbb#..
..#w#bbbbbbbbbb#####bbbbbbbb#bbbbbbbbbbbbbbbb#..
..#w#bbbbbbbbb#######bbbbbb###bbbbbbbbbbbbbbb#..
..#w#bbbbbbbb#########bbbb#####bbbbbbbbbbbbbb#..
..#w#bbbbbbb###########bb#######bbbbbbbbbbbbb#..
..#w#bbbbbb######################bbbbbbbbbbbb#..
..#w#bbbbb########################bbbbbbbbbbb#..
..#w#bbbb##########################bbbbbbbbbb#..
..#w##########################################..
..############################################..
""",
    "page": """
.........######################.........
.........#wwwwwwwwwwwwwwwwwww#w#........
.........#wwwwwwwwwwwwwwwwwww#ww#.......
.........#ww#############www#####.......
.........#wwwwwwwwwwwwwwwwwwwwwww#......
.........#ww##################www#......
.........#wwwwwwwwwwwwwwwwwwwwwww#......
.........#ww#################wwww#......
.........#wwwwwwwwwwwwwwwwwwwwwww#......
.........#ww###################ww#......
.........#wwwwwwwwwwwwwwwwwwwwwww#......
.........#ww##############wwwwwww#......
.........#wwwwwwwwwwwwwwwwwwwwwww#......
.........#ww##################www#......
.........#wwwwwwwwwwwwwwwwwwwwwww#......
.........#########################......
""",
    "pdf": """
.........######################.........
.........#wwwwwwwwwwwwwwwwwww#w#........
.........#wwwwwwwwwwwwwwwwwww#ww#.......
.........#wwwwwwwwwwwwwwwwwww#####......
.........#wbbbbbbbbbbbbbbbbbbbbbw#......
.........#wbwwwbbwwbbbwwwwbbbbbbw#......
.........#wbwbbwbwbwbbwbbbbbbbbbw#......
.........#wbwwwbbwbbwbwwwbbbbbbbw#......
.........#wbwbbbbwbwbbwbbbbbbbbbw#......
.........#wbwbbbbwwbbbwbbbbbbbbbw#......
.........#wbbbbbbbbbbbbbbbbbbbbbw#......
.........#wwwwwwwwwwwwwwwwwwwwwww#......
.........#ww##################www#......
.........#wwwwwwwwwwwwwwwwwwwwwww#......
.........#ww##############wwwwwww#......
.........#########################......
""",
    "picture": """
.........######################.........
.........#wwwwwwwwwwwwwwwwwww#w#........
.........#wwwwwwwwwwwwwwwwwww#ww#.......
.........#w#####################........
.........#w#bbbbbbbbbbbbbbbbbb#w#.......
.........#w#bbbbbbbbbbbbbwwbbb#w#.......
.........#w#bbbbbbbbbbbbwwwwbb#w#.......
.........#w#bbbbbbbbbbbbbwwbbb#w#.......
.........#w#bbbbbb#bbbbbbbbbbb#w#.......
.........#w#bbbbb###bbbbbbbbbb#w#.......
.........#w#bbbb#####bbbb#bbbb#w#.......
.........#w#bbb#######bb###bbb#w#.......
.........#w#bb#################w#.......
.........#w####################w#.......
.........#wwwwwwwwwwwwwwwwwwwwww#.......
.........########################.......
""",
    "install": """
....................######..................
....................#wwww#..................
....................#wwww#..................
....................#wwww#..................
....................#wwww#..................
...............######wwww######.............
................#wwwwwwwwwwwww#.............
.................#wwwwwwwwwww#..............
..................#wwwwwwwww#...............
...................#wwwwwww#................
....................#wwwww#.................
.....................#www#..................
...######################################...
..#bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb#..
..#bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb#..
..#bbbbbbbbbbbbbbb############bbbbbbbbbbbb#..
..#bbbbbbbbbbbbbbb#wwwwwwwwww#bbbbbbbbbbbb#..
..#bbbbbbbbbbbbbbb############bbbbbbbbbbbb#..
..#bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb#..
..########################################..
""",
    "drawer": """
.....##################################.....
....#wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww#....
...#wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww#...
..##########################################
..#bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb#
..#bwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwb#
..#bwbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb#
..#bwbbbbbbbbbbbb##############bbbbbbbbbbbb#
..#bwbbbbbbbbbbbb#wwwwwwwwwwww#bbbbbbbbbbbb#
..#bwbbbbbbbbbbbb#w##########w#bbbbbbbbbbbb#
..#bwbbbbbbbbbbbb##############bbbbbbbbbbbb#
..#bwbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb#
..#bwbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb#
..#bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb#
..##########################################
""",
}


def picture(name: str) -> tuple[int, int, list[list[int]]]:
    rows = [r for r in ART[name].strip("\n").split("\n")]
    w = max(len(r) for r in rows)
    return w, len(rows), [[PENS[c] for c in r.ljust(w, ".")] for r in rows]


def image_record(w: int, h: int, px: list[list[int]]) -> bytes:
    rowbytes = ((w + 15) // 16) * 2
    planes = bytearray()
    for plane in range(2):
        for y in range(h):
            row = bytearray(rowbytes)
            for x in range(w):
                if px[y][x] & (1 << plane):
                    row[x // 8] |= 0x80 >> (x % 8)
            planes += row
    return struct.pack(">hhhhhIBBI", 0, 0, w, h, 2, 1, 0x03, 0, 0) + bytes(planes)


def string(s: str) -> bytes:
    b = s.encode("latin-1") + b"\0"
    return struct.pack(">I", len(b)) + b


def drawer_data(width: int, height: int) -> bytes:
    nw = struct.pack(">hhhhBBIIIIIIIhhHHH", 60, 40, width, height, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 90, 40, 0xFFFF, 0xFFFF, 1)
    return nw + struct.pack(">ii", 0, 0)


def icon(kind: int, art: str, default_tool: str | None = None, tool_types: list[str] | None = None,
         stack: int = 4096, window: tuple[int, int] = (400, 200)) -> bytes:
    """A classic DiskObject: one picture, complemented when selected."""
    w, h, px = picture(art)
    tool_types = tool_types or []
    drawer = kind in (WBDISK, WBDRAWER)
    out = bytearray(struct.pack(">HH", 0xE310, 1))
    # the Gadget: GADGIMAGE with complement highlight, RELVERIFY|GADGIMMEDIATE, BOOLGADGET; UserData 1 = an OS 2.x icon
    out += struct.pack(">IhhhhHHHIIIIIHI", 0, 0, 0, w, h, 0x0004, 0x0003, 0x0001, 1, 0, 0, 0, 0, 0, 1)
    out += struct.pack(">BB", kind, 0)
    out += struct.pack(">IIiiIIi", 1 if default_tool else 0, 1 if tool_types else 0, NO_POSITION, NO_POSITION,
                       1 if drawer else 0, 0, stack)
    if drawer:
        out += drawer_data(*window)
    out += image_record(w, h, px)
    if default_tool:
        out += string(default_tool)
    if tool_types:
        out += struct.pack(">I", (len(tool_types) + 1) * 4)
        for t in tool_types:
            out += string(t)
    if drawer:
        out += struct.pack(">IH", 0, 0)            # dd_Flags, dd_ViewModes: as Workbench chooses
    return bytes(out)


# ---------------------------------------------------------------- test files

def test_png(w: int = 320, h: int = 200) -> bytes:
    rows = []
    for y in range(h):
        r = bytearray([0])
        for x in range(w):
            r += bytes([int(127 + 120 * math.sin(x / 30)), int(127 + 120 * math.sin(y / 22)), int(127 + 120 * math.sin((x + y) / 40))])
        rows.append(bytes(r))

    def chunk(t: bytes, d: bytes) -> bytes:
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))


def test_pdf(title: str) -> bytes:
    """One A4 page with a heading, in plain PDF 1.4."""
    text = f"BT /F1 24 Tf 72 760 Td ({title}) Tj ET\nBT /F1 12 Tf 72 730 Td (If this is on paper, OpenPrint works.) Tj ET\n".encode()
    objs = [b"<< /Type /Catalog /Pages 2 0 R >>",
            b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>",
            b"<< /Length " + str(len(text)).encode() + b" >>\nstream\n" + text + b"endstream",
            b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"]
    out = bytearray(b"%PDF-1.4\n")
    offsets = []
    for i, o in enumerate(objs, 1):
        offsets.append(len(out))
        out += f"{i} 0 obj\n".encode() + o + b"\nendobj\n"
    xref = len(out)
    out += f"xref\n0 {len(objs) + 1}\n0000000000 65535 f \n".encode()
    for off in offsets:
        out += f"{off:010d} 00000 n \n".encode()
    out += f"trailer\n<< /Size {len(objs) + 1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode()
    return bytes(out)


README = """OpenPrint @VERSION@
==================

OpenPrint prints from your Amiga to printers on your network that take
PDF (IPP: most office printers since about 2012), and saves documents as PDF
files. It needs AmigaOS 3.0 or later and a TCP/IP stack (bsdsocket.library)
to reach printers; saving PDF files needs neither.

Installing
----------
Double-click "Install OpenPrint". It puts:
  C:              OpenPrint, OAPPrinters, OAPDiscover, OpenView,
                  OAVWorker
  DEVS:           oapspool.device
  DEVS:Printers   the OpenPrint printer driver
  SYS:WBStartup   the spooler (if you say yes)
and a drawer "OpenPrint" (in SYS:Utilities unless you choose another
place) with OpenView, Printers and Queue, Picture.png and TestPage.pdf.

Trying it
---------
1. Open the OpenPrint drawer and double-click "Printers and Queue". It
   searches your network; choose a printer marked Ready and click "Use for
   printing".
2. Double-click Picture.png. OpenView shows it on the page; set Paper,
   Turn and Size, then choose Print... . The Print window opens on the page.
3. Double-click TestPage.pdf for a PDF: Print... sends it as it is.
4. Programs that print through printer.device: in Prefs/Printer choose the
   printer type OpenPrint, the port "device" and oapspool.device unit 0.
   Their pages then come to the Print window.

Nothing is sent to a printer until you click Print in the Print window. An
upload whose outcome is unclear is never sent a second time by itself.

Licence: MIT, Copyright (c) 2026 Dalsin Limited (see the source repository).
"""


# ---------------------------------------------------------------- the package

def build(version: str, out: Path) -> tuple[Path, list[tuple[str, bytes]]]:
    need = ["OpenPrint", "OAPPrinters", "OAPDiscover", "OpenView", "OAVWorker", "OAPSpooler",
            "oapspool.device", "OpenPrint.driver"]
    missing = [n for n in need if not (BUILD / n).is_file()]
    if missing:
        raise SystemExit(f"build these first (build-amiga.sh, build-browser.sh, build-viewer.sh): {', '.join(missing)}")
    files: list[tuple[str, bytes]] = []
    add = lambda rel, data: files.append((rel, data))
    date = time.strftime("%d.%m.%Y").lstrip("0").replace(".0", ".")
    script = (ROOT / "package" / "Install-OpenPrint").read_text(encoding="latin-1")
    add("Install OpenPrint", script.replace("@VERSION@", version).replace("@DATE@", date).encode("latin-1"))
    add("Install OpenPrint.info", icon(WBPROJECT, "install", "Installer",
                                            ["APPNAME=OpenPrint", "MINUSER=NOVICE", "DEFUSER=NOVICE", "LOGFILE=T:OpenPrint-install.log"]))
    add("ReadMe", README.replace("@VERSION@", version).encode("latin-1"))
    add("ReadMe.info", icon(WBPROJECT, "page", "SYS:Utilities/MultiView"))
    for name in ("OpenPrint", "OAPPrinters", "OAPDiscover", "OpenView", "OAVWorker"):
        add(f"C/{name}", (BUILD / name).read_bytes())
    add("Devs/oapspool.device", (BUILD / "oapspool.device").read_bytes())
    add("Devs/Printers/OpenPrint", (BUILD / "OpenPrint.driver").read_bytes())
    add("WBStartup/OAPSpooler", (BUILD / "OAPSpooler").read_bytes())
    add("WBStartup/OAPSpooler.info", icon(WBTOOL, "printer", tool_types=["DONOTWAIT"], stack=8192))
    # the drawer the script installs: the two programs people open, and something to print
    add("Icons/OpenPrint.info", icon(WBDRAWER, "drawer", window=(420, 150)))   # no icon of its own: Workbench hides it
    add("Drawer/OpenView", (BUILD / "OpenView").read_bytes())
    add("Drawer/OpenView.info", icon(WBTOOL, "viewer", stack=65536))
    add("Drawer/Printers and Queue", (BUILD / "OAPPrinters").read_bytes())
    add("Drawer/Printers and Queue.info", icon(WBTOOL, "printer", stack=65536))
    add("Drawer/Picture.png", test_png())
    add("Drawer/Picture.png.info", icon(WBPROJECT, "picture", "C:OpenView", stack=65536))
    add("Drawer/TestPage.pdf", test_pdf("OpenPrint test page"))
    add("Drawer/TestPage.pdf.info", icon(WBPROJECT, "pdf", "C:OpenView", stack=65536))

    top = out / "OpenPrint"
    if top.exists():
        shutil.rmtree(top)
    for rel, data in files:
        p = top / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
    (out / "OpenPrint.info").write_bytes(icon(WBDRAWER, "drawer", window=(420, 160)))
    return top, files


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--version", default="0.3")
    ap.add_argument("--out", default=str(ROOT / "build" / "package"))
    ap.add_argument("--lha-module", help="the folder with AmigaChrome's lha_archive.py")
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    top, files = build(a.version, out)
    print(f"{top} ({len(files)} files)")
    if a.lha_module:
        sys.path.insert(0, a.lha_module)
        import lha_archive
        members = [lha_archive.Member(f"OpenPrint/{rel}", data) for rel, data in files]
        members.append(lha_archive.Member("OpenPrint.info", (out / "OpenPrint.info").read_bytes()))
        archive = lha_archive.write(out / "OpenPrint.lha", members)
        print(f"{archive} ({archive.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
