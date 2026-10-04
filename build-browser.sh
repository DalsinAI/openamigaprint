#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
CC=${AMIGA_CC:-/home/da1ek/ACNet-compat-lab/toolchain/amiga/bin/m68k-amigaos-gcc}
OUT="$ROOT/build/amigaos3"
mkdir -p "$OUT"
# Strong library-base definitions prevent libnix autolib from opening window.library.
# ReAction classes and bsdsocket.library are opened explicitly by our code.
FLAGS='-m68000 -fno-common -O2 -Wall -Wextra -Werror -Wno-pointer-sign -Wno-misleading-indentation -noixemul'
"$CC" $FLAGS -I"$ROOT/include" "$ROOT"/src/discovery/*.c -o "$OUT/OAPDiscover"
"$CC" $FLAGS -I"$ROOT/include" "$ROOT/src/amiga/printer_browser.c" "$ROOT/src/amiga/selection_events.c" "$ROOT/src/amiga/printer_preferences.c" "$ROOT/src/discovery/protocol.c" -lamiga -o "$OUT/OAPPrinters"
python3 "$ROOT/tools/make_oap_app_icon.py" "$OUT/OAPPrinters.info"
file "$OUT/OAPDiscover" "$OUT/OAPPrinters"

# Isolated native IPC regression; never notifies normal application windows.
"$CC" $FLAGS -DOAP_SELECTION_PREFIX='"OAP.TestSelection."' -I"$ROOT/include" \
 "$ROOT/tests/oap_selection_smoke.c" "$ROOT/src/amiga/selection_events.c" "$ROOT/src/amiga/printer_preferences.c" \
 -o "$OUT/OAPSelectionSmoke"
