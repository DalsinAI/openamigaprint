/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#ifndef OAP_STR_H
#define OAP_STR_H
#include <stddef.h>
#include <string.h>

/* Copies a string into a buffer of `cap` bytes, cut short if it must be,
 * and always ends it. NULL copies as "". (strncpy pads and leaves no end
 * when it fills the buffer; GCC 16 rightly warns about the old copies.) */
static inline void oap_copy(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    if (!cap)
        return;
    if (src)
        while (n + 1 < cap && src[n])
            n++;
    if (n)
        memcpy(dst, src, n);
    dst[n] = 0;
}
#endif
