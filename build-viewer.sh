#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
CC=${AMIGA_CC:-/home/da1ek/ACNet-compat-lab/toolchain/amiga/bin/m68k-amigaos-gcc}
OUT="$ROOT/build/amigaos3"
mkdir -p "$OUT"
FLAGS='-m68000 -O2 -Wall -Wextra -Werror -Wno-pointer-sign -Wno-misleading-indentation -noixemul'
"$CC" $FLAGS -I"$ROOT/include" -o "$OUT/OpenAmigaView" \
 "$ROOT/src/viewer/oav_main.c" "$ROOT/src/viewer/oav_jobs.c" "$ROOT/src/viewer/oav_core.c" -lamiga -lm
"$CC" $FLAGS -I"$ROOT/include" -o "$OUT/OAVWorker" \
 "$ROOT/src/viewer/oav_core.c" "$ROOT/src/viewer/oav_jobs.c" "$ROOT/src/viewer/oav_worker.c" \
 "$ROOT/src/core/job.c" "$ROOT/src/core/ipp.c" "$ROOT/src/amiga/ipp_transport.c" -lamiga -lm
python3 "$ROOT/tools/make_oap_app_icon.py" "$OUT/OpenAmigaView.info"
python3 - "$OUT/OpenAmigaView.info" <<'PYICON'
import struct, sys
from pathlib import Path
p=Path(sys.argv[1]); data=bytearray(p.read_bytes());struct.pack_into('>I',data,74,65536);p.write_bytes(data)
PYICON
file "$OUT/OpenAmigaView" "$OUT/OAVWorker" "$OUT/OpenAmigaView.info"
sha256sum "$OUT/OpenAmigaView" "$OUT/OAVWorker" > "$OUT/VIEWER_SHA256SUMS.txt"
