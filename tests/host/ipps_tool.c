/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* OpenPrint's real network, TLS and IPP code from the command line: the
 * host tests (tests/host/test_ipps.sh) run it against a printer emulator on
 * x86 or ARM64 cores, and the Amiga lab runs it as C:OAPIPPSTest.
 *   ipps_tool query URI          what the printer can do and its certificate
 *   ipps_tool print URI PDF      Print-Job, as C:OAVWorker sends it
 *   ipps_tool trust KEY FP SUBJ  remember a printer's certificate
 * OAP_TLS picks the backend ("opentls" or "amissl"), OAP_TRUST_FILE the
 * remembered certificates, OAP_TLS_CA_FILE an extra authority, and
 * OAP_LOGIN_FILE the logins; on the Amiga, ENV:OpenPrint/TLS,
 * ENV:OpenPrint/TrustedPrinters and ENV:OpenPrint/Logins. */
#include "oap.h"
#include "oap_net.h"
#include "oap_tls.h"
#include "oap_stack.h"
#include <stdio.h>
#include <string.h>

#ifdef __amigaos__
unsigned long __stack = 65536;
static const char oap_version[] __attribute__((used)) = "$VER: OAPIPPSTest 0.1 (9.10.2026)";
#endif

static const char *verdict_name(int v)
{
    static const char *names[] = { "none", "ok", "trusted", "ask", "changed", "name", "dates", "bad", "notls", "failed" };
    return v >= 0 && v <= OAP_CERT_FAILED ? names[v] : "?";
}

static int tool_main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "query")) {
        OAPCaps caps;
        OAPTlsInfo tls;
        char note[256];
        int ok;
        oap_net_start();
        ok = oap_query_pdf(argv[2], &caps, &tls, note, sizeof(note));
        printf("ok=%d pdf=%d verdict=%s backend=%s protocol=%s\nfingerprint=%s\nsubject=%s\nnote=%s\n", ok, caps.pdf,
               verdict_name(tls.verdict), tls.backend, tls.protocol, tls.fingerprint, tls.subject, note);
        oap_net_stop();
        return ok ? 0 : 1;
    }
    if (argc == 4 && !strcmp(argv[1], "print")) {
        OAPJobOptions o;
        char status[300];
        int rc;
        oap_job_defaults(&o);
        snprintf(o.printer_uri, sizeof(o.printer_uri), "%s", argv[2]);
        snprintf(o.job_name, sizeof(o.job_name), "OpenPrint IPPS host test");
        rc = oap_ipp_submit_pdf_ex(argv[3], &o, status, sizeof(status), NULL, NULL);
        printf("rc=%d\nstatus=%s\n", rc, status);
        return rc == OAP_SEND_ACCEPTED ? 0 : 1;
    }
    if (argc == 5 && !strcmp(argv[1], "trust"))
        return oap_trust_remember(argv[2], argv[3], argv[4]) ? 0 : 1;
    fprintf(stderr, "usage: OAPIPPSTest query URI | print URI PDF | trust KEY FINGERPRINT SUBJECT\n");
    return 2;
}

int main(int argc, char **argv)
{
    return oap_main_with_stack(tool_main, argc, argv, 65536);   /* a TLS handshake needs 16 KB of stack */
}
