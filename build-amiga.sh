#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
CC=${AMIGA_CC:-/home/da1ek/ACNet-compat-lab/toolchain/amiga/bin/m68k-amigaos-gcc}
OUT="$ROOT/build/amigaos3"
mkdir -p "$OUT"
"$CC" -m68000 -O2 -Wall -Wextra -Wno-pointer-sign -noixemul -I"$ROOT/include" \
  -o "$OUT/OpenAmigaPrint" \
  "$ROOT/src/core/job.c" "$ROOT/src/core/ipp.c" "$ROOT/src/core/pdf_demo.c" \
  "$ROOT/src/amiga/ipp_transport.c" "$ROOT/src/amiga/ui.c" "$ROOT/src/amiga/queue_ui.c" "$ROOT/src/amiga/main.c" -lamiga
file "$OUT/OpenAmigaPrint"
wc -c "$OUT/OpenAmigaPrint"

BARE="-m68000 -O2 -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -Wall -Wextra -nostartfiles -nostdlib"
"$CC" $BARE -I"$ROOT/include" -o "$OUT/oapspool.device" "$ROOT/src/device/oapspool_device.c" -lgcc
"$CC" -m68000 -O2 -Wall -Wextra -Werror -noixemul -I"$ROOT/include" \
  -o "$OUT/OAPSpooler" "$ROOT/src/spooler/oapspooler.c"
"$CC" -m68000 -O2 -Wall -Wextra -noixemul   -o "$OUT/oapspooltest" "$ROOT/tests/oapspooltest.c"
"$CC" -m68000 -O2 -Wall -Wextra -Werror -noixemul   -o "$OUT/oapstatustest" "$ROOT/tests/oapstatustest.c"
file "$OUT/oapspool.device" "$OUT/OAPSpooler" "$OUT/oapspooltest" "$OUT/oapstatustest"
wc -c "$OUT/oapspool.device" "$OUT/OAPSpooler" "$OUT/oapspooltest" "$OUT/oapstatustest"

"$CC" -m68000 -c -o "$OUT/printertag.o" "$ROOT/src/driver/printertag.s"
"$CC" -m68000 -O2 -Wall -Wextra -fomit-frame-pointer -fno-toplevel-reorder   -fno-builtin -nostdlib -c -o "$OUT/openamigaprint_driver.o"   "$ROOT/src/driver/openamigaprint_driver.c"
"$CC" -m68000 -O2 -Wall -Wextra -fomit-frame-pointer -fno-builtin -nostdlib -c -o "$OUT/oap_arith.o" "$ROOT/src/driver/arith.c"
"$CC" -m68000 -nostartfiles -nostdlib   -Wl,-Map,"$OUT/OpenAmigaPrint.map"   -o "$OUT/OpenAmigaPrint.driver"   "$OUT/printertag.o" "$OUT/openamigaprint_driver.o" "$OUT/oap_arith.o" -lgcc
file "$OUT/OpenAmigaPrint.driver"
wc -c "$OUT/OpenAmigaPrint.driver"

"$CC" -m68000 -O2 -Wall -Wextra -Werror -noixemul   -o "$OUT/oapprinttest" "$ROOT/tests/oapprinttest.c"
file "$OUT/oapprinttest"
wc -c "$OUT/oapprinttest"
"$CC" -m68000 -O2 -Wall -Wextra -Werror -noixemul -I"$ROOT/include" -o "$OUT/oapimagetest" "$ROOT/tests/oapimagetest.c" -lamiga
file "$OUT/oapimagetest"
wc -c "$OUT/oapimagetest"
