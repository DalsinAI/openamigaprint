#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT
# Builds every Amiga program twice: with the os32 GCC 6.5 stove and with the
# os32 GCC 16 stove, into build/gcc65 and build/gcc16. Fails on any warning
# or error from either (every compile has -Werror; this also catches the
# assembler and linker).
#   OS32_GCC65   GCC 6.5's m68k-amigaos-gcc (default ~/AmigaChrome/stoves/os32)
#   OS32_GCC16   GCC 16's m68k-amigaos-gcc (default ~/AmigaChrome/stoves/os32-gcc16)
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
GCC65=${OS32_GCC65:-$HOME/AmigaChrome/stoves/os32/prefix/bin/m68k-amigaos-gcc}
GCC16=${OS32_GCC16:-$HOME/AmigaChrome/stoves/os32-gcc16/prefix/bin/m68k-amigaos-gcc}
rc=0
for pair in "gcc65:$GCC65" "gcc16:$GCC16"; do
    name=${pair%%:*}
    cc=${pair#*:}
    out="$ROOT/build/$name"
    log="$ROOT/build/$name.log"
    mkdir -p "$out"
    "$cc" --version | head -n 1
    if ! (AMIGA_CC="$cc" OAP_OUT="$out" sh "$ROOT/build-amiga.sh" &&
          AMIGA_CC="$cc" OAP_OUT="$out" sh "$ROOT/build-browser.sh" &&
          AMIGA_CC="$cc" OAP_OUT="$out" sh "$ROOT/build-viewer.sh") >"$log" 2>&1; then
        echo "$name: build FAILED (see $log)"
        rc=1
        continue
    fi
    if grep -E 'warning:|error:' "$log"; then
        echo "$name: diagnostics (see $log)"
        rc=1
        continue
    fi
    echo "$name: clean, $(ls "$out" | wc -l) files in build/$name"
done
exit $rc
