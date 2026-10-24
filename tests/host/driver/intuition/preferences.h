/* Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT */
/* Host tests only: the printer fields of struct Preferences the driver reads, and their values as the
 * printer.device autodocs and the Printer editor name them. */
#ifndef INTUITION_PREFERENCES_H
#define INTUITION_PREFERENCES_H
#include <exec/types.h>
struct Preferences {
    UWORD PaperSize, PaperLength, PrintPitch, PrintSpacing, PrintAspect;
};
#define US_LETTER 0x00
#define US_LEGAL 0x10
#define EURO_A3 0x80
#define EURO_A4 0x90
#define EURO_A5 0xA0
#define PICA 0x000
#define ELITE 0x400
#define FINE 0x800
#define SIX_LPI 0x000
#define EIGHT_LPI 0x200
#define ASPECT_HORIZ 0
#define ASPECT_VERT 1
#endif
