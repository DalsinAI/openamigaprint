#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
CC=${AMIGA_CC:-$(command -v m68k-amigaos-gcc || echo m68k-amigaos-gcc)}   # set AMIGA_CC, or have it on PATH
OUT=${OAP_OUT:-"$ROOT/build/amigaos3"}   # set OAP_OUT to build elsewhere
mkdir -p "$OUT"
python3 "$ROOT/tools/make_oap_app_icon.py" "$OUT/OpenPrintTool.info"
# Wall, Wextra and Werror on every 68k compile: the build is clean on GCC 6.5
# and on GCC 16 (scripts/build-compilers.sh builds with both).
APP='-m68000 -fno-common -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -noixemul'
# What printers are reached by: the connection, IPP client and Print-Job.
NET="$ROOT/src/net/conn.c $ROOT/src/net/ipp_client.c $ROOT/src/net/ipp_submit.c"
# The Print requester: it hands sending to C:OAVWorker, so it has no network code.
"$CC" $APP -I"$ROOT/include" \
  -o "$OUT/OpenPrint" \
  "$ROOT/src/core/job.c" \
  "$ROOT/src/amiga/selection_events.c" "$ROOT/src/ui/oap_print_requester.c" "$ROOT/src/ui/oap_gt.c" "$ROOT/src/ui/oap_printers.c" "$ROOT/src/amiga/main.c" "$ROOT/src/amiga/oap_stack.c" "$ROOT/src/viewer/oav_core.c" "$ROOT/src/viewer/oav_jobs.c" -lamiga
file "$OUT/OpenPrint"
wc -c "$OUT/OpenPrint"

BARE="-m68000 -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -Wall -Wextra -Werror -nostartfiles -nostdlib"
"$CC" $BARE -I"$ROOT/include" -o "$OUT/oapspool.device" "$ROOT/src/device/oapspool_device.c" -lgcc
"$CC" $APP -I"$ROOT/include" \
  -o "$OUT/OAPSpooler" "$ROOT/src/spooler/oapspooler.c"
"$CC" $APP -o "$OUT/oapspooltest" "$ROOT/tests/oapspooltest.c"
"$CC" $APP -o "$OUT/oapstatustest" "$ROOT/tests/oapstatustest.c"
file "$OUT/oapspool.device" "$OUT/OAPSpooler" "$OUT/oapspooltest" "$OUT/oapstatustest"
wc -c "$OUT/oapspool.device" "$OUT/OAPSpooler" "$OUT/oapspooltest" "$OUT/oapstatustest"

"$CC" -m68000 -c -o "$OUT/printertag.o" "$ROOT/src/driver/printertag.s"
"$CC" -m68000 -fno-common -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -nostdlib -c -o "$OUT/openprint_driver.o"   "$ROOT/src/driver/openprint_driver.c"
"$CC" -m68000 -fno-common -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -fomit-frame-pointer -fno-builtin -nostdlib -c -o "$OUT/oap_arith.o" "$ROOT/src/driver/arith.c"
"$CC" -m68000 -nostartfiles -nostdlib   -Wl,-Map,"$OUT/OpenPrint.map"   -o "$OUT/OpenPrint.driver"   "$OUT/printertag.o" "$OUT/openprint_driver.o" "$OUT/oap_arith.o" -lgcc
file "$OUT/OpenPrint.driver"
wc -c "$OUT/OpenPrint.driver"

"$CC" $APP -o "$OUT/oapprinttest" "$ROOT/tests/oapprinttest.c"
file "$OUT/oapprinttest"
wc -c "$OUT/oapprinttest"
"$CC" $APP -I"$ROOT/include" -o "$OUT/oapimagetest" "$ROOT/tests/oapimagetest.c" -lamiga
file "$OUT/oapimagetest"
wc -c "$OUT/oapimagetest"

# Print dialogs require the asynchronous worker even without launching the viewer.
"$CC" $APP -I"$ROOT/include" \
  -o "$OUT/OAVWorker" \
  "$ROOT/src/viewer/oav_core.c" "$ROOT/src/viewer/oav_jobs.c" "$ROOT/src/viewer/oav_worker.c" "$ROOT/src/amiga/oap_stack.c" \
  "$ROOT/src/discovery/protocol.c" "$ROOT/src/discovery/http.c" \
  "$ROOT/src/core/job.c" "$ROOT/src/core/ipp.c" $NET -lamiga
file "$OUT/OAVWorker"

python3 "$ROOT/tools/make_oap_icon.py" "$OUT/OpenPrint.info"
file "$OUT/OpenPrint.info"
wc -c "$OUT/OpenPrint.info"
