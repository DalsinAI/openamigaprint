#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Inject a denied mDNS send; no datagram reaches the LAN and no job is printed.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
[ "$(uname -s)" = Linux ] || { echo 'SKIP: send-failure injection uses Linux LD_PRELOAD'; exit 0; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' 0 1 2 15
CC=${HOST_CC:-cc}
"$CC" -shared -fPIC "$ROOT/tests/host/deny_mdns.c" -o "$TMP/deny.so" -ldl
"$CC" -I"$ROOT/include" -O2 -Wall -Wextra -Werror -Wno-misleading-indentation \
    "$ROOT"/src/discovery/*.c -o "$TMP/discover"
rc=0
LD_PRELOAD="$TMP/deny.so" "$TMP/discover" "$TMP/result.tsv" || rc=$?
[ "$rc" -eq 20 ]
grep -q 'Discovery send failed' "$TMP/result.tsv"
grep -q "$(printf 'OAPB1\t1\t')" "$TMP/result.tsv"
printf 'PASS: denied discovery send is reported as a completed error\n'
