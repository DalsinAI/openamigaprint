#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
CC=${AMIGA_CC:-/home/da1ek/ACNet-compat-lab/toolchain/amiga/bin/m68k-amigaos-gcc}
AMISSL_SDK=${AMISSL_SDK:-}
OUT="$ROOT/build/amigaos3"
mkdir -p "$OUT"
if [ -n "$AMISSL_SDK" ]; then
  echo "IPPS: enabled with AmiSSL SDK at $AMISSL_SDK"
  "$CC" -m68000 -O2 -Wall -Wextra -Wno-pointer-sign -noixemul \
    -DOAP_WITH_AMISSL -I"$ROOT/include" -I"$AMISSL_SDK/include" \
    -o "$OUT/OpenAmigaPrint" \
    "$ROOT/src/core/job.c" "$ROOT/src/core/ipp.c" "$ROOT/src/core/pdf_demo.c" \
    "$ROOT/src/amiga/ipp_transport.c" "$ROOT/src/amiga/ui.c" "$ROOT/src/amiga/main.c" \
    -L"$AMISSL_SDK/lib/AmigaOS3" -lamisslstubs -lamiga
else
  echo "IPPS: disabled (set AMISSL_SDK to enable it)"
  "$CC" -m68000 -O2 -Wall -Wextra -Wno-pointer-sign -noixemul \
    -I"$ROOT/include" -o "$OUT/OpenAmigaPrint" \
    "$ROOT/src/core/job.c" "$ROOT/src/core/ipp.c" "$ROOT/src/core/pdf_demo.c" \
    "$ROOT/src/amiga/ipp_transport.c" "$ROOT/src/amiga/ui.c" "$ROOT/src/amiga/main.c" -lamiga
fi
file "$OUT/OpenAmigaPrint"
wc -c "$OUT/OpenAmigaPrint"

BARE="-m68000 -O2 -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -Wall -Wextra -nostartfiles -nostdlib"
"$CC" $BARE -o "$OUT/oapspool.device" "$ROOT/src/device/oapspool_device.c" -lgcc
"$CC" -m68000 -O2 -Wall -Wextra -noixemul   -o "$OUT/oapspooltest" "$ROOT/tests/oapspooltest.c"
file "$OUT/oapspool.device" "$OUT/oapspooltest"
wc -c "$OUT/oapspool.device" "$OUT/oapspooltest"

"$CC" -m68000 -c -o "$OUT/printertag.o" "$ROOT/src/driver/printertag.s"
"$CC" -m68000 -O2 -Wall -Wextra -fomit-frame-pointer -fno-toplevel-reorder   -fno-builtin -nostdlib -c -o "$OUT/openamigaprint_driver.o"   "$ROOT/src/driver/openamigaprint_driver.c"
"$CC" -m68000 -O2 -Wall -Wextra -fomit-frame-pointer -fno-builtin -nostdlib -c -o "$OUT/oap_arith.o" "$ROOT/src/driver/arith.c"
"$CC" -m68000 -nostartfiles -nostdlib   -Wl,-Map,"$OUT/OpenAmigaPrint.map"   -o "$OUT/OpenAmigaPrint.driver"   "$OUT/printertag.o" "$OUT/openamigaprint_driver.o" "$OUT/oap_arith.o" -lgcc
file "$OUT/OpenAmigaPrint.driver"
wc -c "$OUT/OpenAmigaPrint.driver"

"$CC" -m68000 -O2 -Wall -Wextra -Werror -noixemul   -o "$OUT/oapprinttest" "$ROOT/tests/oapprinttest.c"
file "$OUT/oapprinttest"
wc -c "$OUT/oapprinttest"
