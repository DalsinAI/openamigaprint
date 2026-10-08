#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
CC=${AMIGA_CC:-$(command -v m68k-amigaos-gcc || echo m68k-amigaos-gcc)}   # set AMIGA_CC, or have it on PATH
OUT=${OAP_OUT:-"$ROOT/build/amigaos3"}   # set OAP_OUT to build elsewhere
mkdir -p "$OUT"
# Strong library-base definitions prevent libnix autolib from opening window.library.
# ReAction classes and bsdsocket.library are opened explicitly by our code.
FLAGS='-m68000 -fno-common -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -noixemul'
"$CC" $FLAGS -I"$ROOT/include" "$ROOT"/src/discovery/*.c "$ROOT/src/core/ipp.c" "$ROOT/src/net/conn.c" "$ROOT/src/net/ipp_client.c" "$ROOT/src/amiga/oap_stack.c" -o "$OUT/OAPDiscover"
# Printers and Queue (the 3 Oct 2026 redesign): the browser and the queue in one ReAction window.
"$CC" $FLAGS -I"$ROOT/include" "$ROOT/src/ui/oap_printers_window.c" "$ROOT/src/ui/oap_gt.c" "$ROOT/src/ui/oap_printers.c" "$ROOT/src/amiga/oap_stack.c" "$ROOT/src/amiga/selection_events.c" "$ROOT/src/amiga/printer_preferences.c" "$ROOT/src/discovery/protocol.c" "$ROOT/src/core/pdf_demo.c" -lamiga -o "$OUT/OAPPrinters"
python3 "$ROOT/tools/make_oap_app_icon.py" "$OUT/OAPPrinters.info"
file "$OUT/OAPDiscover" "$OUT/OAPPrinters"

# Isolated native IPC regression; never notifies normal application windows.
"$CC" $FLAGS -DOAP_SELECTION_PREFIX='"OAP.TestSelection."' -I"$ROOT/include" \
 "$ROOT/tests/oap_selection_smoke.c" "$ROOT/src/amiga/selection_events.c" "$ROOT/src/amiga/printer_preferences.c" \
 -o "$OUT/OAPSelectionSmoke"
