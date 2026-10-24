/* Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT */
/* Host tests only: exec's memory calls, from tests/test_driver.c (which counts them). */
#ifndef PROTO_EXEC_H
#define PROTO_EXEC_H
#include <exec/types.h>
APTR AllocMem(ULONG size, ULONG flags);
void FreeMem(APTR mem, ULONG size);
void CopyMem(const void *from, APTR to, ULONG size);
#endif
