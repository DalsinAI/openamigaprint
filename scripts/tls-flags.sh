# Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT
# Sourced by the build scripts: the TLS backend for the programs that talk
# to printers (C:OAPDiscover and C:OAVWorker). Sets TLS_FLAGS and TLS_SOURCES.
# OpenTLS (opentls.library, API 1) only: its headers are in
# third_party/opentls, and the library is opened at run time. No AmiSSL SDK
# is needed or used (AmiSSL was a second backend until 19 October 2026).
# Plain ipp:// needs no TLS library.
TLS_FLAGS="-DOAP_TLS_OPENTLS -I$ROOT/third_party/opentls/include"
TLS_SOURCES="$ROOT/src/tls/tls.c $ROOT/src/tls/trust.c $ROOT/src/tls/tls_opentls.c"
