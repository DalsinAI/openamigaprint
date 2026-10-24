/* Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT */
/* Host tests only: the PrinterData and PrinterExtendedData fields the driver uses. */
#ifndef DEVICES_PRTBASE_H
#define DEVICES_PRTBASE_H
#include <exec/types.h>
#include <intuition/preferences.h>
#define PRT_STDARGS
struct DeviceData { APTR dd_ExecBase; };
struct PrinterData {
    struct DeviceData pd_Device;
    LONG (*pd_PWrite)(APTR data, LONG len);
    LONG (*pd_PBothReady)(void);
    struct Preferences pd_Preferences;
    UBYTE *pd_PrintBuf;
};
struct PrinterExtendedData {
    UWORD ped_XDotsInch, ped_YDotsInch, ped_MaxColumns;
    ULONG ped_MaxXDots, ped_MaxYDots;
};
#endif
