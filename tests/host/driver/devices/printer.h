/* Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT */
/* Host tests only: the printer.device command numbers and flags the driver uses. */
#ifndef DEVICES_PRINTER_H
#define DEVICES_PRINTER_H
#include <exec/types.h>
#define aRIS 0
#define aRIN 1
#define aIND 2
#define aNEL 3
#define SPECIAL_DENSITYMASK 0x700
#define SPECIAL_DENSITY1 0x100
#define SPECIAL_DENSITY2 0x200
#define SPECIAL_DENSITY3 0x300
#define SPECIAL_DENSITY4 0x400
#define SPECIAL_DENSITY5 0x500
#define SPECIAL_DENSITY6 0x600
#define SPECIAL_DENSITY7 0x700
#define SPECIAL_NOFORMFEED 0x800
#define PDERR_NOERR 0
#define PDERR_CANCEL 1
#define PDERR_BADDIMENSION 4
#define PDERR_BUFFERMEMORY 7
struct IODRPReq { int unused; };
#endif
