/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#ifndef OAP_STACK_H
#define OAP_STACK_H
/* Runs a program's body on a stack of at least `bytes`. The libnix these
 * programs link has no stack swapping, so `__stack` alone does nothing: a
 * program started with Run from a boot-time shell gets 4 KB, which the
 * ReAction windows and path buffers overrun (the overrun corrupted the heap
 * and hung OpenView in malloc). Workbench start-ups pass through. */
#ifdef __amigaos__
int oap_main_with_stack(int (*body)(int, char **), int argc, char **argv, unsigned long bytes);
#else
/* Elsewhere (host builds of the tools) the stack is the system's. */
static inline int oap_main_with_stack(int (*body)(int, char **), int argc, char **argv, unsigned long bytes)
{
    (void)bytes;
    return body(argc, argv);
}
#endif
#endif
