#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT
# IPPS end to end on x86 or ARM64 cores, with no internet: OpenPrint's own
# connection, TLS, IPP and Print-Job code against printers on 127.0.0.1:
#   - CUPS's ippeveprinter with its own self-signed certificate (as most
#     printers have), and a second one with a certificate from a test
#     authority made here;
#   - tests/host/auth_printer.py, which asks for a login (Digest over
#     IPPS; Basic over plain IPP, where OpenPrint must refuse to answer).
# Both TLS backends run: OpenSSL in place of AmiSSL, and OpenTLS: its own
# code built for the host when OPENTLS names an openamigatls checkout with
# build-host/ built, else its calls on OpenSSL (tests/host/opentls_host.c).
# Needs ippeveprinter (cups-ipp-utils), openssl, python3 and the OpenSSL
# headers; skips without them. Files stay in build/ipps-lab (each run's
# printed jobs in build/ipps-lab/runs/).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
LAB="$ROOT/build/ipps-lab"
BASE=${OAP_TEST_PORT:-19641}
P_SELF=$BASE P_CA=$((BASE + 1)) P_AUTH=$((BASE + 2)) P_PLAIN=$((BASE + 3))
for tool in ippeveprinter openssl python3; do
    command -v $tool >/dev/null 2>&1 || { echo "SKIP: $tool is not installed"; exit 0; }
done
[ -f /usr/include/openssl/ssl.h ] || { echo "SKIP: no OpenSSL headers"; exit 0; }
RUN=$(date +%Y%m%d-%H%M%S)-$$
SPOOL="$LAB/runs/$RUN/spool"            # each run's jobs apart (job files are named by job number)
mkdir -p "$SPOOL" "$LAB/runs/$RUN/spool-ca" "$LAB/keys-self" "$LAB/keys-ca" "$LAB/ca"
cd "$LAB"

# A test authority and a certificate it signs for localhost (and only that name).
if [ ! -f ca/ca.crt ]; then
    openssl req -x509 -newkey rsa:2048 -nodes -keyout ca/ca.key -out ca/ca.crt -days 30 -subj "/CN=OpenPrint Test CA" 2>/dev/null
    openssl req -newkey rsa:2048 -nodes -keyout keys-ca/localhost.key -out ca/srv.csr -subj "/CN=localhost" 2>/dev/null
    printf 'subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\nextendedKeyUsage=serverAuth\n' > ca/ext.cnf
    openssl x509 -req -in ca/srv.csr -CA ca/ca.crt -CAkey ca/ca.key -CAcreateserial -out keys-ca/localhost.crt \
        -days 30 -extfile ca/ext.cnf 2>/dev/null
fi

PIDS=""
stop() { for p in $PIDS; do kill "$p" 2>/dev/null || true; done; }
trap stop 0 1 2 15
# -r off: no DNS-SD advertisement on the LAN; -k: jobs stay in spool/, counted
ippeveprinter -r off -k -n localhost -p $P_SELF -d "$SPOOL" -K "$LAB/keys-self" -f application/pdf -F application/pdf \
    "OpenPrint IPPS self" > ippeve-self.log 2>&1 &
PIDS="$PIDS $!"
ippeveprinter -r off -k -n localhost -p $P_CA -d "$LAB/runs/$RUN/spool-ca" -K "$LAB/keys-ca" -f application/pdf -F application/pdf \
    "OpenPrint IPPS CA" > ippeve-ca.log 2>&1 &
PIDS="$PIDS $!"
: > auth.log
: > plain.log
python3 -I "$ROOT/tests/host/auth_printer.py" $P_AUTH keys-ca/localhost.crt keys-ca/localhost.key "$LAB/auth.log" &
PIDS="$PIDS $!"
python3 -I "$ROOT/tests/host/auth_printer.py" $P_PLAIN - - "$LAB/plain.log" --plain --basic &
PIDS="$PIDS $!"

# The OpenTLS backend links the real OpenTLS (on BearSSL) when OPENTLS names
# an openamigatls checkout whose host libraries are built (cmake -S . -B
# build-host); otherwise tests/host/opentls_host.c, its calls on OpenSSL.
if [ -n "${OPENTLS:-}" ] && [ -f "$OPENTLS/build-host/libopentls.a" ]; then
    OT_INC="-I$OPENTLS/host/include -I$OPENTLS/include"
    OT_LIB="$OPENTLS/build-host/libopentls.a $OPENTLS/build-host/libbearssl.a $OPENTLS/build-host/libopencrypto.a"
    OT_WHAT="opentls.library's own code (OpenTLS on BearSSL, host build)"
    export OPENTLS_ROOT="$LAB/opentls-root"      # its ENV:OpenTLS
    mkdir -p "$OPENTLS_ROOT"
    # as an OpenTLS install has it: a CA bundle (this machine's), which none
    # of the test printers is signed by; OPENTLS_NO_BUNDLE=1 tests without one
    if [ -z "${OPENTLS_NO_BUNDLE:-}" ] && [ -f /etc/ssl/certs/ca-certificates.crt ]; then
        cp /etc/ssl/certs/ca-certificates.crt "$OPENTLS_ROOT/ca-bundle.pem"
    else
        : > "$OPENTLS_ROOT/ca-bundle.pem"
    fi
else
    OT_INC="-I$ROOT/third_party/opentls/include -I$ROOT/tests/host/amiga"
    OT_LIB="$ROOT/tests/host/opentls_host.c"
    OT_WHAT="tests/host/opentls_host.c (OpenTLS's calls on OpenSSL)"
fi
echo "OpenTLS backend runs on: $OT_WHAT"
cc -std=c11 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -Werror -I"$ROOT/include" $OT_INC \
   -DOAP_TLS_OSSL -DOAP_TLS_OPENTLS -o ipps_tool "$ROOT/tests/host/ipps_tool.c" \
   "$ROOT"/src/net/*.c "$ROOT"/src/tls/*.c "$ROOT/src/core/ipp.c" "$ROOT/src/core/job.c" "$ROOT/src/core/pdf_demo.c" \
   "$ROOT/src/discovery/protocol.c" "$ROOT/src/discovery/http.c" $OT_LIB -lssl -lcrypto
printf '%%PDF-1.4\n%% OpenPrint IPPS host test\n%%%%EOF\n' > page.pdf
sleep 2

fails=0 checks=0
check() {   # check NAME EXPECTED-REGEX OUTPUT
    checks=$((checks + 1))
    if printf '%s\n' "$3" | grep -Eq "$2"; then echo "ok - $1"; else echo "FAIL - $1"; printf '%s\n' "$3" | sed 's/^/    /'; fails=$((fails + 1)); fi
}
idle() {    # wait until the emulator has finished its last job
    i=0
    while [ $i -lt 30 ] && ! ./ipps_tool query "$1" 2>/dev/null | grep -q 'ready'; do sleep 1; i=$((i + 1)); done
}
spooled() { ls "$SPOOL" | wc -l; }
SELF_FP=$(echo | openssl s_client -connect localhost:$P_SELF -servername localhost 2>/dev/null |
          openssl x509 -noout -fingerprint -sha256 | sed 's/.*=//')
CA_FP=$(openssl x509 -in keys-ca/localhost.crt -noout -fingerprint -sha256 | sed 's/.*=//')
export OAP_TRUST_FILE="$LAB/trusted.txt" OAP_LOGIN_FILE="$LAB/logins.txt"
printf 'localhost:%s\tkim\tCircle Of Life\n' $P_AUTH > logins.txt

# Plain IPP needs no TLS library at all.
out=$(OAP_TLS=none ./ipps_tool print ipp://localhost:$P_SELF/ipp/print page.pdf || true)
check "plain ipp:// prints with no TLS library" 'rc=1' "$out"
out=$(OAP_TLS=none ./ipps_tool query ipps://localhost:$P_SELF/ipp/print || true)
check "ipps:// with no TLS library says what to install" 'verdict=notls.*|needs opentls.library' "$out"

for backend in amissl opentls; do
    export OAP_TLS=$backend
    : > trusted.txt
    idle ipp://localhost:$P_SELF/ipp/print
    before=$(spooled)
    out=$(./ipps_tool query ipps://localhost:$P_SELF/ipp/print || true)
    check "$backend: self-signed printer is asked about, with its fingerprint" "verdict=ask" "$out"
    check "$backend: the fingerprint is the printer's" "fingerprint=$SELF_FP" "$out"
    check "$backend: who it is issued to" "subject=localhost" "$out"
    check "$backend: it still says it takes PDF" "ok=1 pdf=1" "$out"
    out=$(./ipps_tool print ipps://localhost:$P_SELF/ipp/print page.pdf || true)
    check "$backend: no job to a printer not trusted yet" "rc=0.*|isn't trusted yet" "$out"
    check "$backend: and nothing reached it" "^$before\$" "$(spooled)"
    ./ipps_tool trust localhost:$P_SELF "$SELF_FP" localhost
    out=$(./ipps_tool query ipps://localhost:$P_SELF/ipp/print || true)
    check "$backend: trusted by its fingerprint" "verdict=trusted" "$out"
    out=$(./ipps_tool print ipps://localhost:$P_SELF/ipp/print page.pdf || true)
    check "$backend: prints over IPPS once trusted" "Accepted job [0-9]+ by localhost over IPPS" "$out"
    check "$backend: the job reached the printer" "^$((before + 1))\$" "$(spooled)"
    idle ipp://localhost:$P_SELF/ipp/print
    ./ipps_tool trust localhost:$P_SELF 00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF old
    out=$(./ipps_tool query ipps://localhost:$P_SELF/ipp/print || true)
    check "$backend: another certificate than the trusted one is 'changed'" "verdict=changed" "$out"
    out=$(./ipps_tool print ipps://localhost:$P_SELF/ipp/print page.pdf || true)
    check "$backend: and gets no job" "has changed since you trusted it" "$out"

    out=$(OAP_TLS_CA_FILE="$LAB/ca/ca.crt" ./ipps_tool query ipps://localhost:$P_CA/ipp/print || true)
    check "$backend: signed by a trusted authority for its name" "verdict=ok" "$out"
    out=$(OAP_TLS_CA_FILE="$LAB/ca/ca.crt" ./ipps_tool query ipps://127.0.0.1:$P_CA/ipp/print || true)
    check "$backend: the same certificate by address is for another name" "verdict=name" "$out"
    out=$(./ipps_tool query ipps://localhost:$P_CA/ipp/print || true)
    check "$backend: without that authority it is asked about" "verdict=ask" "$out"
    idle ipp://localhost:$P_CA/ipp/print
    out=$(OAP_TLS_CA_FILE="$LAB/ca/ca.crt" ./ipps_tool print ipps://localhost:$P_CA/ipp/print page.pdf || true)
    check "$backend: prints to the authority-signed printer" "over IPPS" "$out"
    out=$(OAP_TLS_CA_FILE="$LAB/ca/ca.crt" ./ipps_tool print ipps://127.0.0.1:$P_CA/ipp/print page.pdf || true)
    check "$backend: refuses a certificate for another name" "another name" "$out"

    # A printer that asks for a login: Digest, only over a trusted TLS connection.
    : > auth.log
    out=$(./ipps_tool print ipps://localhost:$P_AUTH/ipp/print page.pdf || true)
    check "$backend: no password to a login printer not trusted yet" "isn't trusted yet" "$out"
    check "$backend: (no Authorization was sent)" "^$" "$(grep -v 'auth=none' auth.log || true)"
    ./ipps_tool trust localhost:$P_AUTH "$CA_FP" localhost
    : > auth.log
    out=$(./ipps_tool print ipps://localhost:$P_AUTH/ipp/print page.pdf || true)
    check "$backend: Digest login over IPPS prints" "Accepted job 7 by localhost over IPPS" "$out"
    check "$backend: the printer saw a good Digest login for the job" "op=0x0002 auth=good" "$(cat auth.log)"
done
unset OAP_TLS

# A plain ipp:// printer that asks for a password gets none.
out=$(./ipps_tool print ipp://localhost:$P_PLAIN/ipp/print page.pdf || true)
check "no password over plain ipp://" "only over ipps://" "$out"
check "(the plain printer never saw an Authorization header)" "^$" "$(grep -v 'auth=none' plain.log || true)"

echo "$checks checks, $fails failed"
[ $fails -eq 0 ] && echo "PASS: IPPS end to end against local printers, both TLS backends; no internet"
[ $fails -eq 0 ]
