/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* The TLS abstraction (include/oap_tls.h): picks a backend, applies the
 * remembered certificates, and reads what a certificate says. */
#include "oap_tls.h"
#include "oap_str.h"
#include "tls_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __amigaos__
#include <dos/var.h>
#include <proto/dos.h>
#endif

struct OAPTls {
    const OAPTlsBackend *backend;
    void *session;
};

static const OAPTlsBackend *const backends[] = {
#ifdef OAP_TLS_OPENTLS
    &oap_tls_opentls,
#endif
#ifdef OAP_TLS_OSSL
    &oap_tls_ossl,
#endif
    NULL
};

static const OAPTlsBackend *chosen;

int oap_tls_setting(const char *name, char *out, size_t cap)
{
#ifdef __amigaos__
    char var[64];
    snprintf(var, sizeof(var), "OpenPrint/%s", name);
    if (GetVar((STRPTR)var, (STRPTR)out, (LONG)cap, GVF_GLOBAL_ONLY) > 0) {
        out[strcspn(out, "\r\n")] = 0;
        return out[0] != 0;
    }
    return 0;
#else
    char var[64];
    const char *v;
    size_t i;
    snprintf(var, sizeof(var), "OAP_%s", name);
    for (i = 4; var[i]; i++)
        if (var[i] >= 'a' && var[i] <= 'z')
            var[i] = (char)(var[i] - 32);
    v = getenv(var);
    if (!v || !v[0])
        return 0;
    oap_copy(out, cap, v);
    return 1;
#endif
}

static int wanted(const OAPTlsBackend *b, const char *only)
{
    if (!only[0])
        return 1;
    if (!strcmp(only, "none"))
        return 0;
    return (!strcmp(only, "opentls") && !strcmp(b->name, "OpenTLS")) ||
           (!strcmp(only, "amissl") && !strcmp(b->name, "AmiSSL"));
}

int oap_tls_available(char *why, size_t why_cap)
{
    char only[16] = "", reason[160] = "";
    size_t i;
    if (chosen)
        return 1;
    oap_tls_setting("TLS", only, sizeof(only));
    for (i = 0; backends[i]; i++) {
        if (!wanted(backends[i], only))
            continue;
        if (backends[i]->open(reason, sizeof(reason))) {
            chosen = backends[i];
            return 1;
        }
    }
    snprintf(why, why_cap, "This printer uses IPPS (encrypted IPP), which needs opentls.library%s. "
             "Install it, or use the printer's ipp:// address.",
#ifdef OAP_TLS_OSSL
             " or AmiSSL 5"
#else
             ""
#endif
             );
    return 0;
}

int oap_tls_look_reconnects(void)
{
    return chosen && chosen->look_needs_reconnect;
}

const char *oap_tls_backend(void)
{
    return chosen ? chosen->name : "";
}

void oap_tls_stop(void)
{
    if (chosen)
        chosen->close();
    chosen = NULL;
}

int oap_cert_usable(int verdict)
{
    return verdict == OAP_CERT_OK || verdict == OAP_CERT_TRUSTED;
}

const char *oap_cert_words(int verdict)
{
    switch (verdict) {
    case OAP_CERT_NONE: return "Not encrypted";
    case OAP_CERT_OK: return "Encrypted; certificate checked";
    case OAP_CERT_TRUSTED: return "Encrypted; the certificate you trusted";
    case OAP_CERT_ASK: return "Its own certificate: trust it to print";
    case OAP_CERT_CHANGED: return "Certificate changed since you trusted it";
    case OAP_CERT_NAME: return "Certificate is for another name";
    case OAP_CERT_DATES: return "Certificate out of date (check the clock)";
    case OAP_CERT_BAD: return "Certificate unreadable or badly signed";
    case OAP_CERT_NOTLS: return "Needs opentls.library for IPPS";
    default: return "Encrypted connection failed";
    }
}

static int is_cert_problem(int verdict)
{
    return verdict == OAP_CERT_ASK || verdict == OAP_CERT_NAME || verdict == OAP_CERT_DATES ||
           verdict == OAP_CERT_BAD || verdict == OAP_CERT_CHANGED;
}

OAPTls *oap_tls_start(long fd, const char *host, const char *trust_key, int mode,
                      OAPTlsInfo *info, char *err, size_t err_cap)
{
    unsigned char pin[32];
    int pinned;
    OAPTls *t;
    memset(info, 0, sizeof(*info));
    info->verdict = OAP_CERT_FAILED;
    if (!oap_tls_available(err, err_cap)) {
        info->verdict = OAP_CERT_NOTLS;
        return NULL;
    }
    oap_copy(info->backend, sizeof(info->backend), chosen->name);
    t = calloc(1, sizeof(*t));
    if (!t) {
        snprintf(err, err_cap, "Not enough memory for the encrypted connection");
        return NULL;
    }
    t->backend = chosen;
    pinned = trust_key && oap_trust_lookup(trust_key, pin);
    t->session = chosen->start(fd, host, pinned ? pin : NULL, mode, info, err, err_cap);
    if (info->have_fingerprint)
        oap_fingerprint_text(info->sha256, info->fingerprint, sizeof(info->fingerprint));
    /* a remembered printer showing another certificate is never just "ask" */
    if (pinned && info->have_fingerprint && memcmp(pin, info->sha256, 32) && is_cert_problem(info->verdict))
        info->verdict = OAP_CERT_CHANGED;
    if (pinned && info->have_fingerprint && !memcmp(pin, info->sha256, 32) && info->verdict == OAP_CERT_OK)
        info->verdict = OAP_CERT_TRUSTED;
    if (t->session && mode == OAP_TLS_VERIFY && !oap_cert_usable(info->verdict)) {
        chosen->end(t->session);           /* a backend that let it through: refuse it here */
        t->session = NULL;
    }
    if (!t->session) {
        if (is_cert_problem(info->verdict) || !err[0])
            snprintf(err, err_cap, "%s", oap_cert_words(info->verdict));
        free(t);
        return NULL;
    }
    return t;
}

long oap_tls_read(OAPTls *t, void *buf, size_t n, unsigned millis)
{
    return t->backend->read(t->session, buf, n, millis);
}

long oap_tls_write(OAPTls *t, const void *data, size_t n, unsigned millis)
{
    return t->backend->write(t->session, data, n, millis);
}

void oap_tls_end(OAPTls *t)
{
    if (!t)
        return;
    t->backend->end(t->session);
    free(t);
}

/* ---- what a certificate says ------------------------------------------ */

/* One DER element at p[*at]: its tag, and where its contents are. */
static int der_next(const unsigned char *p, size_t len, size_t *at, unsigned *tag, size_t *start, size_t *size)
{
    size_t x = *at, n;
    if (x + 2 > len)
        return 0;
    *tag = p[x++];
    n = p[x++];
    if (n & 0x80) {
        unsigned k = n & 0x7f;
        if (!k || k > 3 || x + k > len)
            return 0;
        for (n = 0; k; k--)
            n = (n << 8) | p[x++];
    }
    if (n > len - x)
        return 0;
    *start = x;
    *size = n;
    *at = x + n;
    return 1;
}

static void der_text(const unsigned char *p, size_t n, char *out, size_t cap)
{
    size_t i, k = 0;
    if (!cap)
        return;
    for (i = 0; i < n && k + 1 < cap; i++)
        out[k++] = p[i] < 32 || p[i] == 127 ? '?' : (char)p[i];
    out[k] = 0;
}

int oap_cert_der_names(const unsigned char *der, size_t len, char *subject, size_t subject_cap,
                       char *until, size_t until_cap)
{
    static const unsigned char cn_oid[] = { 0x55, 0x04, 0x03 };
    size_t at = 0, s, n, tbs_end;
    unsigned tag;
    int i;
    if (subject_cap)
        subject[0] = 0;
    if (until_cap)
        until[0] = 0;
    if (!der_next(der, len, &at, &tag, &s, &n) || tag != 0x30)       /* Certificate */
        return 0;
    at = s;
    if (!der_next(der, len, &at, &tag, &s, &n) || tag != 0x30)       /* TBSCertificate */
        return 0;
    tbs_end = s + n;
    at = s;
    /* [0] version, serial, signature, issuer, validity, subject */
    for (i = 0; i < 6; i++) {
        if (!der_next(der, tbs_end, &at, &tag, &s, &n))
            return 0;
        if (i == 0 && tag != 0xa0)
            i++;                                   /* version 1: no [0] */
        if (i == 4 && tag == 0x30) {               /* validity: notBefore, notAfter */
            size_t v = s, vs, vn;
            unsigned vt;
            if (der_next(der, s + n, &v, &vt, &vs, &vn) && der_next(der, s + n, &v, &vt, &vs, &vn) && until_cap > 10) {
                const unsigned char *d = der + vs;
                if (vt == 0x17 && vn >= 6)         /* UTCTime YYMMDD */
                    snprintf(until, until_cap, "%s%c%c-%c%c-%c%c", d[0] < '5' ? "20" : "19", d[0], d[1], d[2], d[3], d[4], d[5]);
                else if (vt == 0x18 && vn >= 8)    /* GeneralizedTime YYYYMMDD */
                    snprintf(until, until_cap, "%c%c%c%c-%c%c-%c%c", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
            }
        }
        if (i == 5 && tag == 0x30) {               /* subject: SET of SEQ { OID, value } */
            size_t r = s, end = s + n;
            while (r < end) {
                size_t ss, sn, a, as, an, o, os, on;
                unsigned st, at2, ot;
                if (!der_next(der, end, &r, &st, &ss, &sn) || st != 0x31)
                    break;
                a = ss;
                while (a < ss + sn && der_next(der, ss + sn, &a, &at2, &as, &an)) {
                    o = as;
                    if (at2 == 0x30 && der_next(der, as + an, &o, &ot, &os, &on) && ot == 0x06 && on == 3 &&
                        !memcmp(der + os, cn_oid, 3) && der_next(der, as + an, &o, &ot, &os, &on)) {
                        der_text(der + os, on, subject, subject_cap);
                        return 1;
                    }
                }
            }
            return until_cap && until[0];
        }
    }
    return 0;
}
