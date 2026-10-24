/* Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT */
/* Host tests only: the PrtInfo fields the driver's TRANSFER reads. */
#ifndef DEVICES_PRTGFX_H
#define DEVICES_PRTGFX_H
#include <exec/types.h>
#define PCMYELLOW 0
#define PCMMAGENTA 1
#define PCMCYAN 2
#define PCMBLACK 3
#define PCMBLUE PCMYELLOW
#define PCMGREEN PCMMAGENTA
#define PCMRED PCMCYAN
union colorEntry { ULONG colorLong; UBYTE colorByte[4]; };
struct PrtInfo {
    union colorEntry *pi_ColorInt;
    UWORD *pi_ScaleX;
    UWORD pi_width, pi_xpos;
};
#endif
