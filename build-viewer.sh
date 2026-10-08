#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
CC=${AMIGA_CC:-$(command -v m68k-amigaos-gcc || echo m68k-amigaos-gcc)}   # set AMIGA_CC, or have it on PATH
OUT="$ROOT/build/amigaos3"
mkdir -p "$OUT"
FLAGS='-m68000 -fno-common -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -Wno-pointer-sign -Wno-misleading-indentation -noixemul'
"$CC" $FLAGS -I"$ROOT/include" -o "$OUT/OpenView" \
 "$ROOT/src/amiga/printer_selection.c" "$ROOT/src/amiga/selection_events.c" "$ROOT/src/viewer/oav_main.c" "$ROOT/src/viewer/oav_jobs.c" "$ROOT/src/viewer/oav_core.c" "$ROOT/src/ui/oap_printers.c" "$ROOT/src/ui/oap_gt.c" "$ROOT/src/amiga/oap_stack.c" -lamiga
"$CC" $FLAGS -I"$ROOT/include" -o "$OUT/OAVWorker" \
 "$ROOT/src/viewer/oav_core.c" "$ROOT/src/viewer/oav_jobs.c" "$ROOT/src/viewer/oav_worker.c" "$ROOT/src/amiga/oap_stack.c" \
 "$ROOT/src/discovery/protocol.c" "$ROOT/src/discovery/http.c" "$ROOT/src/discovery/network.c" "$ROOT/src/core/job.c" "$ROOT/src/core/ipp.c" "$ROOT/src/amiga/ipp_transport.c" -lamiga
python3 "$ROOT/tools/make_oap_app_icon.py" "$OUT/OpenView.info"
python3 - "$OUT/OpenView.info" <<'PYICON'
import struct, sys
from pathlib import Path
p=Path(sys.argv[1]); data=bytearray(p.read_bytes());struct.pack_into('>I',data,74,65536);p.write_bytes(data)
PYICON
file "$OUT/OpenView" "$OUT/OAVWorker" "$OUT/OpenView.info"
sha256sum "$OUT/OpenView" "$OUT/OAVWorker" > "$OUT/VIEWER_SHA256SUMS.txt"

# No-network guest request/worker regression helper.
"$CC" $FLAGS -I"$ROOT/include" -o "$OUT/OAVRequestSmoke" \
 "$ROOT/tests/oav_request_smoke.c" "$ROOT/src/viewer/oav_jobs.c" "$ROOT/src/viewer/oav_core.c" -lamiga
