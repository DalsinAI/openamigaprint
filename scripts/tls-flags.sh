# Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT
# Sourced by the build scripts: the TLS backends for the programs that talk
# to printers (C:OAPDiscover and C:OAVWorker). Sets TLS_FLAGS and TLS_SOURCES.
#   - OpenTLS (opentls.library, API 1): always; its headers are in
#     third_party/opentls, and the library is opened at run time.
#   - AmiSSL 5: when its SDK's include folder is found (AMISSL, default
#     ~/AmigaChrome-dev/sdk/amissl/AmiSSL/Developer/include). Headers only:
#     AmiSSL is opened at run time, never linked in.
# Plain ipp:// needs neither.
TLS_FLAGS="-DOAP_TLS_OPENTLS -I$ROOT/third_party/opentls/include"
TLS_SOURCES="$ROOT/src/tls/tls.c $ROOT/src/tls/trust.c $ROOT/src/tls/tls_opentls.c"
AMISSL=${AMISSL:-$HOME/AmigaChrome-dev/sdk/amissl/AmiSSL/Developer/include}
if [ -f "$AMISSL/proto/amissl.h" ]; then
    TLS_FLAGS="$TLS_FLAGS -DOAP_TLS_OSSL -I$AMISSL"
    TLS_SOURCES="$TLS_SOURCES $ROOT/src/tls/tls_ossl.c"
else
    echo "IPPS: AmiSSL 5 SDK not found at $AMISSL; building with OpenTLS only (set AMISSL to add AmiSSL)" >&2
fi
