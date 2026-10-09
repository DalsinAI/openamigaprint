#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT
# Inject a denied mDNS send; no datagram reaches the LAN and no job is printed.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
[ "$(uname -s)" = Linux ] || { echo 'SKIP: send-failure injection uses Linux LD_PRELOAD'; exit 0; }
OUT="$ROOT/build/sendfail"          # kept for a look afterwards; each run overwrites it
mkdir -p "$OUT"
CC=${HOST_CC:-cc}
"$CC" -shared -fPIC "$ROOT/tests/host/deny_mdns.c" -o "$OUT/deny.so" -ldl
"$CC" -I"$ROOT/include" -O2 -Wall -Wextra -Werror \
    "$ROOT"/src/discovery/*.c "$ROOT/src/core/ipp.c" "$ROOT/src/net/conn.c" "$ROOT/src/net/ipp_client.c" \
    -o "$OUT/discover"
rc=0
LD_PRELOAD="$OUT/deny.so" "$OUT/discover" "$OUT/result.tsv" || rc=$?
[ "$rc" -eq 20 ]
grep -q 'Discovery send failed' "$OUT/result.tsv"
grep -q "$(printf 'OAPB1\t1\t')" "$OUT/result.tsv"
printf 'PASS: denied discovery send is reported as a completed error\n'
