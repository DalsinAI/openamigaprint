#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
CC=${AMIGA_CC:-$(command -v m68k-amigaos-gcc || echo m68k-amigaos-gcc)}   # set AMIGA_CC, or have it on PATH
OUT="$ROOT/build/amigaos3"
mkdir -p "$OUT"
python3 "$ROOT/tools/make_oap_app_icon.py" "$OUT/OpenPrintTool.info"
"$CC" -m68000 -fno-common -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -Wno-pointer-sign -Wno-misleading-indentation -noixemul -I"$ROOT/include" \
  -o "$OUT/OpenPrint" \
  "$ROOT/src/core/job.c" "$ROOT/src/core/ipp.c" "$ROOT/src/core/pdf_demo.c" \
  "$ROOT/src/discovery/protocol.c" "$ROOT/src/discovery/http.c" "$ROOT/src/discovery/network.c" "$ROOT/src/amiga/printer_selection.c" "$ROOT/src/amiga/selection_events.c" "$ROOT/src/amiga/printer_preferences.c" "$ROOT/src/amiga/ipp_transport.c" "$ROOT/src/ui/oap_print_requester.c" "$ROOT/src/ui/oap_gt.c" "$ROOT/src/ui/oap_printers.c" "$ROOT/src/amiga/main.c" "$ROOT/src/amiga/oap_stack.c" "$ROOT/src/viewer/oav_core.c" "$ROOT/src/viewer/oav_jobs.c" -lamiga
file "$OUT/OpenPrint"
wc -c "$OUT/OpenPrint"

BARE="-m68000 -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -Wall -Wextra -nostartfiles -nostdlib"
"$CC" $BARE -I"$ROOT/include" -o "$OUT/oapspool.device" "$ROOT/src/device/oapspool_device.c" -lgcc
"$CC" -m68000 -fno-common -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -noixemul -I"$ROOT/include" \
  -o "$OUT/OAPSpooler" "$ROOT/src/spooler/oapspooler.c"
"$CC" -m68000 -fno-common -O2 -fno-delete-null-pointer-checks -Wall -Wextra -noixemul   -o "$OUT/oapspooltest" "$ROOT/tests/oapspooltest.c"
"$CC" -m68000 -fno-common -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -noixemul   -o "$OUT/oapstatustest" "$ROOT/tests/oapstatustest.c"
file "$OUT/oapspool.device" "$OUT/OAPSpooler" "$OUT/oapspooltest" "$OUT/oapstatustest"
wc -c "$OUT/oapspool.device" "$OUT/OAPSpooler" "$OUT/oapspooltest" "$OUT/oapstatustest"

"$CC" -m68000 -c -o "$OUT/printertag.o" "$ROOT/src/driver/printertag.s"
"$CC" -m68000 -fno-common -O2 -fno-delete-null-pointer-checks -Wall -Wextra -fomit-frame-pointer -fno-toplevel-reorder   -fno-builtin -nostdlib -c -o "$OUT/openprint_driver.o"   "$ROOT/src/driver/openprint_driver.c"
"$CC" -m68000 -fno-common -O2 -fno-delete-null-pointer-checks -Wall -Wextra -fomit-frame-pointer -fno-builtin -nostdlib -c -o "$OUT/oap_arith.o" "$ROOT/src/driver/arith.c"
"$CC" -m68000 -nostartfiles -nostdlib   -Wl,-Map,"$OUT/OpenPrint.map"   -o "$OUT/OpenPrint.driver"   "$OUT/printertag.o" "$OUT/openprint_driver.o" "$OUT/oap_arith.o" -lgcc
file "$OUT/OpenPrint.driver"
wc -c "$OUT/OpenPrint.driver"

"$CC" -m68000 -fno-common -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -noixemul   -o "$OUT/oapprinttest" "$ROOT/tests/oapprinttest.c"
file "$OUT/oapprinttest"
wc -c "$OUT/oapprinttest"
"$CC" -m68000 -fno-common -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -noixemul -I"$ROOT/include" -o "$OUT/oapimagetest" "$ROOT/tests/oapimagetest.c" -lamiga
file "$OUT/oapimagetest"
wc -c "$OUT/oapimagetest"

# Print dialogs require the asynchronous worker even without launching the viewer.
"$CC" -m68000 -fno-common -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -Wno-pointer-sign -Wno-misleading-indentation -noixemul -I"$ROOT/include" \
  -o "$OUT/OAVWorker" \
  "$ROOT/src/viewer/oav_core.c" "$ROOT/src/viewer/oav_jobs.c" "$ROOT/src/viewer/oav_worker.c" "$ROOT/src/amiga/oap_stack.c" \
  "$ROOT/src/discovery/protocol.c" "$ROOT/src/discovery/http.c" "$ROOT/src/discovery/network.c" \
  "$ROOT/src/core/job.c" "$ROOT/src/core/ipp.c" "$ROOT/src/amiga/ipp_transport.c" -lamiga
file "$OUT/OAVWorker"

python3 "$ROOT/tools/make_oap_icon.py" "$OUT/OpenPrint.info"
file "$OUT/OpenPrint.info"
wc -c "$OUT/OpenPrint.info"
