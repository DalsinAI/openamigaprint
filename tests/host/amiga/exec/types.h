/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* Host tests only: the few AmigaOS types the OpenTLS headers use, so the
 * OpenTLS backend compiles on x86 or ARM64 cores against
 * tests/host/opentls_host.c. Not the NDK. */
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H
#include <stdint.h>
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int16_t WORD;
typedef uint16_t UWORD;
typedef uint8_t UBYTE;
typedef void *APTR;
typedef const void *CONST_APTR;
typedef unsigned char *STRPTR;
typedef const unsigned char *CONST_STRPTR;
#define VOID void
#define CONST const
#endif
