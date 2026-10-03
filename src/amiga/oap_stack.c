/* SPDX-License-Identifier: BSD-2-Clause */
/* See include/oap_stack.h. */
#include "oap_stack.h"
#include <exec/types.h>
#include <exec/tasks.h>
#include <exec/memory.h>
#include <proto/exec.h>

/* Static, not on the stack: nothing may be read from the old stack's frame
 * between the two StackSwap calls. */
static struct StackSwapStruct swap;
static int (*volatile swap_body)(int, char **);
static volatile int swap_argc, swap_rc;
static char **volatile swap_argv;

int oap_main_with_stack(int (*body)(int, char **), int argc, char **argv, unsigned long bytes)
{
    struct Task *me = FindTask(NULL);
    unsigned long have = (unsigned long)me->tc_SPUpper - (unsigned long)me->tc_SPLower;
    APTR lower;

    if (have >= bytes || !(lower = AllocVec(bytes, MEMF_ANY)))
        return body(argc, argv);
    swap.stk_Lower = lower;
    swap.stk_Upper = (ULONG)lower + bytes;
    swap.stk_Pointer = (APTR)swap.stk_Upper;
    swap_body = body;
    swap_argc = argc;
    swap_argv = argv;
    StackSwap(&swap);
    swap_rc = swap_body(swap_argc, swap_argv);
    StackSwap(&swap);
    FreeVec(lower);
    return swap_rc;
}
