/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#ifndef OAP_TLS_H
#define OAP_TLS_H
/* OpenPrint's TLS abstraction: IPPS (IPP over TLS) without making one TLS
 * library mandatory. Plain ipp:// never comes here and needs no TLS
 * library at all.
 *
 * Backends, tried in this order in each task that asks:
 *   - opentls.library (OpenTLS, on BearSSL; MIT licensed and free), API 1;
 *   - AmiSSL 5 (OpenSSL 3), when the build had its SDK (OAP_TLS_OSSL);
 * on x86 or ARM64 cores (the host tests), OpenSSL itself stands in for
 * AmiSSL, and a test build of OpenTLS's calls on OpenSSL stands in for
 * opentls.library, so both backends' code runs against a real printer
 * emulator. ENV:OpenPrint/TLS set to "opentls", "amissl" or "none" makes a
 * task use only that one (for testing).
 *
 * Certificates: a printer's certificate is accepted when an authority the
 * library trusts signed it for the printer's name, or when it is the very
 * certificate the user chose to trust for that printer (its SHA-256
 * fingerprint, remembered in ENVARC:OpenPrint/TrustedPrinters by host and
 * port). Printers mostly make their own (self-signed) certificates, so
 * the second is the usual case: the first connection reports
 * OAP_CERT_ASK with the fingerprint, Printers and Queue asks "Trust this
 * printer?", and from then on that certificate, and only that one, is
 * accepted. */
#include <stddef.h>

enum {
    OAP_CERT_NONE = 0,      /* plain ipp://: no TLS */
    OAP_CERT_OK,            /* signed by a trusted authority, for this name */
    OAP_CERT_TRUSTED,       /* the certificate the user trusted for this printer */
    OAP_CERT_ASK,           /* no trusted authority signed it (self-signed): ask */
    OAP_CERT_CHANGED,       /* not the certificate the user trusted for this printer */
    OAP_CERT_NAME,          /* signed, but for another name */
    OAP_CERT_DATES,         /* expired or not valid yet (check the Amiga's clock) */
    OAP_CERT_BAD,           /* malformed, a bad signature or an unsupported key */
    OAP_CERT_NOTLS,         /* ipps:// but no TLS library in this task */
    OAP_CERT_FAILED         /* the TLS handshake failed for another reason */
};

/* What a TLS connection found out about the printer's certificate. */
typedef struct OAPTlsInfo {
    int verdict;                       /* OAP_CERT_* */
    int have_fingerprint;
    unsigned char sha256[32];
    char fingerprint[96];              /* "AB:CD:...", 32 bytes */
    char subject[128];                 /* who it is issued to, when the backend says */
    char problem[200];                 /* the backend's own words, when not OK */
    char protocol[16], cipher[64];
    char backend[16];                  /* "OpenTLS" or "AmiSSL" */
} OAPTlsInfo;

/* 1 when the verdict lets a print job (or a password) go to the printer. */
int oap_cert_usable(int verdict);
/* The verdict in plain words, for a status line. */
const char *oap_cert_words(int verdict);

/* How a connection treats a certificate it cannot accept:
 * OAP_TLS_VERIFY refuses it (the TLS session is not made);
 * OAP_TLS_LOOK goes on anyway, for read-only questions such as "can it
 * print PDF?", and reports the verdict, so the printer can be shown and
 * the user asked. Nothing private is ever sent on a LOOK connection. */
enum { OAP_TLS_VERIFY = 0, OAP_TLS_LOOK = 1 };

typedef struct OAPTls OAPTls;

/* 1 when a TLS library is open in this task; otherwise 0 and `why` says,
 * in words, what to install. Opens it on first use. */
int oap_tls_available(char *why, size_t why_cap);
/* "OpenTLS", "AmiSSL", or "" when none is open. */
const char *oap_tls_backend(void);
/* Closes the TLS library (each task, at the end, after oap_net_stop's peers). */
void oap_tls_stop(void);

/* TLS as the client over the connected, non-blocking socket `fd`. `host`
 * goes in SNI (unless it is an address) and is checked against the
 * certificate; `trust_key` ("host:port") finds a remembered certificate.
 * NULL on failure: info->verdict and `err` say why. */
OAPTls *oap_tls_start(long fd, const char *host, const char *trust_key, int mode,
                      OAPTlsInfo *info, char *err, size_t err_cap);
/* As oap_sock_recv/oap_sock_send (src/net/oap_sock.h), through TLS. */
long oap_tls_read(OAPTls *t, void *buf, size_t n, unsigned millis);
long oap_tls_write(OAPTls *t, const void *data, size_t n, unsigned millis);
/* Says goodbye (close_notify) and frees it; the socket stays open. */
void oap_tls_end(OAPTls *t);

/* ---- remembered certificates (src/tls/trust.c) ---------------------- */
#define OAP_TRUST_VAR "OpenPrint/TrustedPrinters"
/* One line per printer: host:port <TAB> fingerprint <TAB> who it is issued to. */
int oap_trust_lookup(const char *trust_key, unsigned char sha256[32]);
int oap_trust_remember(const char *trust_key, const char *fingerprint, const char *subject);
/* "host:port" for a URI's printer. */
void oap_trust_key(const char *host, unsigned port, char *key, size_t cap);
/* sha256 as "AB:CD:..."; and back (1 when it is 32 bytes). */
void oap_fingerprint_text(const unsigned char sha256[32], char *out, size_t cap);
int oap_fingerprint_parse(const char *text, unsigned char sha256[32]);
#endif
