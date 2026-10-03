#!/usr/bin/env python3
"""Generate the classic Workbench tool icon for the OpenAmigaPrint queue app."""
from __future__ import annotations
import argparse, struct
from pathlib import Path
WIDTH, HEIGHT = 48, 32
WBTOOL = 3
NO_ICON_POSITION = -0x80000000

def artwork(selected: bool) -> list[list[int]]:
    px=[[0 for _ in range(WIDTH)] for _ in range(HEIGHT)]
    def rect(x0,y0,x1,y1,edge,fill):
        for y in range(y0,y1+1):
            for x in range(x0,x1+1): px[y][x]=fill
        for x in range(x0,x1+1): px[y0][x]=px[y1][x]=edge
        for y in range(y0,y1+1): px[y][x0]=px[y][x1]=edge
    paper=3 if selected else 2
    body=2 if selected else 3
    # Three queued sheets behind the printer.
    rect(18,1,34,10,1,paper)
    rect(14,4,30,13,1,paper)
    rect(10,7,26,16,1,paper)
    for yy in (9,11,13):
        for xx in range(13,24): px[yy][xx]=1 if selected else 3
    # Printer body and slot.
    rect(7,14,40,26,1,body)
    rect(11,16,36,19,1,0)
    for x in range(13,35): px[18][x]=paper
    rect(35,16,38,19,1,paper)
    # Output job sheet.
    rect(15,22,34,30,1,paper)
    for y in (25,27):
        for x in range(18,32): px[y][x]=1 if selected else 3
    return px

def bitplanes(px):
    padded=(WIDTH+15)&~15; out=bytearray()
    for plane in range(2):
        for y in range(HEIGHT):
            for x0 in range(0,padded,8):
                value=0
                for bit in range(8):
                    x=x0+bit; c=px[y][x] if x<WIDTH else 0
                    if c&(1<<plane): value|=1<<(7-bit)
                out.append(value)
    return bytes(out)

def image_record(px):
    return struct.pack(">hhhhhIBBI",0,0,WIDTH,HEIGHT,2,0,0x03,0,0)+bitplanes(px)

def build_icon():
    parts=[struct.pack(">HH",0xE310,1)]
    parts.append(struct.pack(">IhhhhHHHIIIiIHI",0,0,0,WIDTH,HEIGHT,0x0006,0x0003,0x0001,1,1,0,0,0,0,1))
    parts.append(struct.pack(">BB",WBTOOL,0))
    parts.append(struct.pack(">II",0,0))
    parts.append(struct.pack(">ii",NO_ICON_POSITION,NO_ICON_POSITION))
    parts.append(struct.pack(">II",0,0))
    parts.append(struct.pack(">i",65536))
    parts.append(image_record(artwork(False))); parts.append(image_record(artwork(True)))
    raw=b"".join(parts)
    expected=78+2*(20+(WIDTH*HEIGHT*2//8))
    if len(raw)!=expected: raise RuntimeError((len(raw),expected))
    if raw[0x30]!=WBTOOL: raise RuntimeError("wrong icon type")
    return raw

def main():
    ap=argparse.ArgumentParser();ap.add_argument("output",nargs="?",default="build/amigaos3/OpenAmigaPrintTool.info")
    out=Path(ap.parse_args().output);out.parent.mkdir(parents=True,exist_ok=True);raw=build_icon();out.write_bytes(raw)
    print(f"OpenAmigaPrint tool icon: {out} ({len(raw)} bytes)")
    return 0
if __name__=="__main__": raise SystemExit(main())
