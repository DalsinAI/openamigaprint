/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* Host tests only (see exec/types.h here): a hook is called directly. */
#ifndef UTILITY_HOOKS_H
#define UTILITY_HOOKS_H
#include <exec/types.h>
struct MinNode { struct MinNode *mln_Succ, *mln_Pred; };
typedef ULONG (*HOOKFUNC)();
struct Hook {
    struct MinNode h_MinNode;
    HOOKFUNC h_Entry;
    HOOKFUNC h_SubEntry;
    APTR h_Data;
};
#endif
