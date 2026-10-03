/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef OAP_STACK_H
#define OAP_STACK_H
/* Runs a program's body on a stack of at least `bytes`. The libnix these
 * programs link has no stack swapping, so `__stack` alone does nothing: a
 * program started with Run from a boot-time shell gets 4 KB, which the
 * ReAction windows and path buffers overrun (the overrun corrupted the heap
 * and hung OpenAmigaView in malloc). Workbench start-ups pass through. */
int oap_main_with_stack(int (*body)(int, char **), int argc, char **argv, unsigned long bytes);
#endif
