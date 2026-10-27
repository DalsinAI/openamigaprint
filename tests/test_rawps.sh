#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT
# Raw PostScript to IPP printers, end to end on x86 or ARM64 cores: OpenPrint's own connection, IPP and
# Print-Job code (OAPIPPSTest's, as C:OAVWorker sends) against stand-in printers on 127.0.0.1 only
# (tests/host/ps_printer.py, which prints nothing), and CUPS's ippeveprinter too when it is installed.
#   - PostScript reaches a printer that takes it byte for byte, with document-format, job-name (its %%Title)
#     and copies, and none of a PDF's page settings;
#   - a printer that doesn't list PostScript is asked, and sent nothing; one that lists it and then refuses
#     the job gets a plain "not printed"; one that never answers gives up within the waits (no hang);
#   - a PDF still goes as before (every setting), and PCL is never sent.
# Needs python3; skips without it. Files stay in build/rawps (the stand-ins' spools and logs).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
OUT="$ROOT/build/rawps"
BASE=${OAP_TEST_PORT:-19661}
command -v python3 >/dev/null 2>&1 || { echo "SKIP: python3 is not installed"; exit 0; }
rm -rf "$OUT"; mkdir -p "$OUT"
cd "$OUT"
CC=${HOST_CC:-cc}
"$CC" -std=c11 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -Werror -I"$ROOT/include" -o ipps_tool "$ROOT/tests/host/ipps_tool.c" \
    "$ROOT"/src/net/*.c "$ROOT/src/tls/tls.c" "$ROOT/src/tls/trust.c" "$ROOT/src/core/ipp.c" "$ROOT/src/core/job.c" \
    "$ROOT/src/discovery/protocol.c" "$ROOT/src/discovery/http.c"

# A PostScript job as a program writes it (its title in the DSC header), a PDF, and PCL.
printf '%%!PS-Adobe-3.0\n%%%%Title: (Quarterly report)\n%%%%Creator: PageStream\n%%%%Pages: 1\n%%%%EndComments\n/Courier findfont 12 scalefont setfont\n72 720 moveto (OpenPrint raw PostScript) show\nshowpage\n%%%%EOF\n' > report.ps
printf '\004%%!PS-Adobe-2.0\n%%%%EndComments\nshowpage\n' > untitled.ps
printf '%%PDF-1.4\n%% OpenPrint raw PostScript test\n%%%%EOF\n' > page.pdf
printf '\033E\033&l0O OpenPrint PCL\f\033E' > page.pcl

PIDS=""
stop() { for p in $PIDS; do kill "$p" 2>/dev/null || true; done; }
trap stop 0 1 2 15
standin() {   # standin NAME PORT FORMATS MODE
    python3 -I "$ROOT/tests/host/ps_printer.py" --bind 127.0.0.1 --port "$2" --spool "$OUT/$1" --formats "$3" \
        --mode "$4" --name "OpenPrint test $1" --log "$OUT/$1.log" > /dev/null 2>&1 &
    PIDS="$PIDS $!"
}
P_PS=$BASE P_PDF=$((BASE + 1)) P_LIE=$((BASE + 2)) P_MUTE=$((BASE + 3)) P_EVE=$((BASE + 4))
standin ps $P_PS application/pdf,application/postscript normal
standin pdfonly $P_PDF application/pdf normal
standin liar $P_LIE application/pdf,application/postscript refuse
standin mute $P_MUTE application/pdf,application/postscript silent
for i in 1 2 3 4 5 6 7 8 9 10; do
    [ -f ps.log ] && [ -f pdfonly.log ] && [ -f liar.log ] && [ -f mute.log ] && break; sleep 0.3
done

fails=0 checks=0
check() {   # check NAME EXPECTED-REGEX OUTPUT
    checks=$((checks + 1))
    if printf '%s\n' "$3" | grep -Eq "$2"; then echo "ok - $1"; else echo "FAIL - $1"; printf '%s\n' "$3" | sed 's/^/    /'; fails=$((fails + 1)); fi
}
job() {       # job SPOOL N FIELD: an attribute of the kept job N (or bytes, sha256)
    python3 -I - "$1/job-$2.json" "$3" <<'EOF'
import json, sys
r = json.load(open(sys.argv[1]))
k = sys.argv[2]
if k in ("bytes", "sha256"):
    print(r["document-" + k])
else:
    print(";".join(str(a["value"]) for a in r["attributes"] if a["name"] == k))
EOF
}
sha() { python3 -I -c 'import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"; }

out=$(./ipps_tool query ipp://127.0.0.1:$P_PS/ipp/print || true)
check "a PostScript printer is seen to take PostScript" "ok=1 pdf=1 ps=1" "$out"
out=$(./ipps_tool query ipp://127.0.0.1:$P_PDF/ipp/print || true)
check "a PDF-only printer is seen not to" "ok=1 pdf=1 ps=2" "$out"

out=$(./ipps_tool print ipp://127.0.0.1:$P_PS/ipp/print report.ps 2 "Raw PostScript" || true)
check "PostScript to a printer that takes it: accepted" "rc=1" "$out"
check "the document arrived byte for byte" "^$(sha report.ps)\$" "$(job ps 1 sha256)"
check "and all of it" "^$(wc -c < report.ps | tr -d ' ')\$" "$(job ps 1 bytes)"
check "document-format application/postscript" "^application/postscript\$" "$(job ps 1 document-format)"
check "job-name is the document's own %%Title" "^Quarterly report\$" "$(job ps 1 job-name)"
check "copies as asked" "^2\$" "$(job ps 1 copies)"
check "none of a PDF's page settings" "^\$" "$(job ps 1 media)$(job ps 1 sides)$(job ps 1 orientation-requested)$(job ps 1 print-color-mode)$(job ps 1 page-ranges)"
out=$(./ipps_tool print ipp://127.0.0.1:$P_PS/ipp/print untitled.ps || true)
check "PostScript after a driver's end-of-job byte: accepted" "rc=1" "$out"
check "as it is, the ^D too" "^$(sha untitled.ps)\$" "$(job ps 2 sha256)"
check "no %%Title: the name it was given" "^OpenPrint IPPS host test\$" "$(job ps 2 job-name)"

asked=$(grep -c 'Get-Printer-Attributes' pdfonly.log)
out=$(./ipps_tool print ipp://127.0.0.1:$P_PDF/ipp/print report.ps || true)
check "a printer without PostScript: not sent, and says why" "rc=0.*|doesn't take PostScript" "$out"
check "it was only asked what it takes" "^0 $((asked + 1))\$" "$(grep -c 'Print-Job' pdfonly.log || true) $(grep -c 'Get-Printer-Attributes' pdfonly.log)"
check "and nothing was kept there" "^0\$" "$(ls pdfonly | wc -l | tr -d ' ')"
out=$(./ipps_tool print ipp://127.0.0.1:$P_LIE/ipp/print report.ps || true)
check "a printer that refuses the PostScript job: not printed" "Not printed: the printer doesn't take PostScript \\(IPP status 0x040a\\)" "$out"
start=$(date +%s)
out=$(./ipps_tool print ipp://127.0.0.1:$P_MUTE/ipp/print report.ps || true)
took=$(( $(date +%s) - start ))
check "a printer that never answers: a plain error" "rc=0" "$out"
check "within the waits, no hang (${took} s)" "^yes\$" "$([ $took -le 30 ] && echo yes || echo no)"

out=$(./ipps_tool print ipp://127.0.0.1:$P_PS/ipp/print page.pdf || true)
check "a PDF still goes" "rc=1" "$out"
check "as a PDF" "^application/pdf\$" "$(job ps 3 document-format)"
check "with its page settings, as before" "^iso_a4_210x297mm one-sided monochrome\$" "$(job ps 3 media) $(job ps 3 sides) $(job ps 3 print-color-mode)"
check "byte for byte" "^$(sha page.pdf)\$" "$(job ps 3 sha256)"
n=$(grep -c 'Print-Job' ps.log)
out=$(./ipps_tool print ipp://127.0.0.1:$P_PS/ipp/print page.pcl || true)
check "PCL is never sent" "rc=0.*|neither" "$out"
check "nothing reached the printer for it" "^$n\$" "$(grep -c 'Print-Job' ps.log)"

# CUPS's own printer emulator, when it is here: an independent IPP implementation, strict about requests.
if command -v ippeveprinter >/dev/null 2>&1; then
    mkdir -p eve
    ippeveprinter -r off -k -n localhost -p $P_EVE -d "$OUT/eve" -f application/pdf,application/postscript \
        -F application/pdf "OpenPrint PS eve" > eve.log 2>&1 &
    PIDS="$PIDS $!"
    i=0; while [ $i -lt 30 ] && ! ./ipps_tool query ipp://127.0.0.1:$P_EVE/ipp/print 2>/dev/null | grep -q 'ps=1'; do sleep 1; i=$((i + 1)); done
    out=$(./ipps_tool print ipp://127.0.0.1:$P_EVE/ipp/print report.ps 3 || true)
    check "ippeveprinter takes the PostScript job" "rc=1" "$out"
    i=0; while [ $i -lt 15 ] && ! ls eve/*.ps >/dev/null 2>&1; do sleep 1; i=$((i + 1)); done
    kept=$(ls eve/*.ps 2>/dev/null | head -1)
    check "and keeps it byte for byte" "^$(sha report.ps)\$" "$([ -n "$kept" ] && sha "$kept" || echo none)"
else
    echo "(ippeveprinter not installed: its two checks skipped)"
fi

echo "$((checks - fails)) of $checks raw PostScript checks passed (127.0.0.1 only; nothing printed)"
[ $fails -eq 0 ] && echo "PASS: raw PostScript to IPP printers" || { echo "FAIL: raw PostScript"; exit 1; }
