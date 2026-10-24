/* Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT */
/* Host tests only: the AmigaOS types and the few structure fields OpenPrint's printer driver
 * (src/driver/openprint_driver.c) uses, so tests/test_driver.c can run it on x86 or ARM64 cores.
 * Not the NDK, and not its layouts: only the names. */
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H
#include <stdint.h>
/* pointer-sized here: the driver's Render() passes pointers in a LONG, as the Amiga's 32 bits allow */
typedef intptr_t LONG;
typedef uintptr_t ULONG;
typedef int16_t WORD;
typedef uint16_t UWORD;
typedef int8_t BYTE;
typedef uint8_t UBYTE;
typedef char TEXT;
typedef void *APTR;
typedef unsigned char *STRPTR;
#define VOID void
#endif
