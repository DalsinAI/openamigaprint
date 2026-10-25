#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT
# OpenPrefs' Printing page's own writer and reader (openamigaprefs Printing/src/pr_prefs.c, Look/src/pf_iff.c)
# against the Print window's paper.c: tests/host/paper_page.c. Skips with no openamigaprefs checkout beside this
# one; OPENAMIGAPREFS=dir names another. Files only; nothing is printed.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
OP=${OPENAMIGAPREFS:-$ROOT/../openamigaprefs}
if [ ! -f "$OP/Printing/src/pr_prefs.c" ] || [ ! -f "$OP/Look/src/pf_iff.c" ]; then
    echo "SKIP: no openamigaprefs checkout at $OP (OPENAMIGAPREFS=dir): the Printing page against the Print window"
    exit 0
fi
mkdir -p "$ROOT/build"
${HOST_CC:-cc} -std=gnu99 -O2 -Wall -Wextra -Werror -I"$ROOT/include" -I"$OP/Printing/src" -I"$OP/Look/src" \
    -o "$ROOT/build/test_paper_page" "$ROOT/tests/host/paper_page.c" "$ROOT/src/core/paper.c" \
    "$OP/Printing/src/pr_prefs.c" "$OP/Look/src/pf_iff.c"
"$ROOT/build/test_paper_page"
