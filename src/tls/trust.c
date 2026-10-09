/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* Printers' certificates the user chose to trust (include/oap_tls.h):
 * ENV:OpenPrint/TrustedPrinters and its ENVARC: copy, one line each:
 *   host:port <TAB> AB:CD:...(SHA-256) <TAB> who it is issued to
 * On x86 or ARM64 cores (host tests) the file named by OAP_TRUST_FILE. */
#include "oap_tls.h"
#include "oap_str.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __amigaos__
#include <exec/types.h>
#include <dos/var.h>
#include <proto/dos.h>
#endif

#define TRUST_MAX 8192

static long load(char *buf, size_t cap)
{
#ifdef __amigaos__
    LONG got = GetVar((STRPTR)OAP_TRUST_VAR, (STRPTR)buf, (LONG)cap, GVF_GLOBAL_ONLY | GVF_BINARY_VAR);
    if (got < 0)
        got = 0;
    if ((size_t)got >= cap)
        got = (LONG)cap - 1;
    buf[got] = 0;
    return got;
#else
    const char *path = getenv("OAP_TRUST_FILE");
    FILE *f = fopen(path ? path : "build/trusted-printers.txt", "rb");
    size_t got = 0;
    if (f) {
        got = fread(buf, 1, cap - 1, f);
        fclose(f);
    }
    buf[got] = 0;
    return (long)got;
#endif
}

static int store(const char *buf, size_t len)
{
#ifdef __amigaos__
    return SetVar((STRPTR)OAP_TRUST_VAR, (STRPTR)buf, (LONG)len, GVF_GLOBAL_ONLY | GVF_SAVE_VAR | GVF_BINARY_VAR) != 0;
#else
    const char *path = getenv("OAP_TRUST_FILE");
    FILE *f = fopen(path ? path : "build/trusted-printers.txt", "wb");
    int ok;
    if (!f)
        return 0;
    ok = fwrite(buf, 1, len, f) == len;
    if (fclose(f))
        ok = 0;
    return ok;
#endif
}

void oap_trust_key(const char *host, unsigned port, char *key, size_t cap)
{
    char *p;
    snprintf(key, cap, "%s:%u", host, port);
    for (p = key; *p; p++)                     /* names are not case-sensitive */
        if (*p >= 'A' && *p <= 'Z')
            *p = (char)(*p + 32);
}

void oap_fingerprint_text(const unsigned char sha256[32], char *out, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i, k = 0;
    if (!cap)
        return;
    for (i = 0; i < 32 && k + 3 < cap; i++) {
        if (i)
            out[k++] = ':';
        out[k++] = hex[sha256[i] >> 4];
        out[k++] = hex[sha256[i] & 15];
    }
    out[k] = 0;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

int oap_fingerprint_parse(const char *text, unsigned char sha256[32])
{
    size_t n = 0;
    while (*text && n < 32) {
        int hi, lo;
        if (*text == ':' || *text == ' ') {
            text++;
            continue;
        }
        hi = hexval((unsigned char)text[0]);
        lo = text[1] ? hexval((unsigned char)text[1]) : -1;
        if (hi < 0 || lo < 0)
            return 0;
        sha256[n++] = (unsigned char)(hi << 4 | lo);
        text += 2;
    }
    while (*text == ':' || *text == ' ' || *text == '\r' || *text == '\n')
        text++;
    return n == 32 && !*text;
}

/* The line for `key` in buf: its start, or NULL. */
static char *find_line(char *buf, const char *key)
{
    size_t k = strlen(key);
    char *line = buf;
    while (line && *line) {
        if (!strncmp(line, key, k) && line[k] == '\t')
            return line;
        line = strchr(line, '\n');
        if (line)
            line++;
    }
    return NULL;
}

int oap_trust_lookup(const char *trust_key, unsigned char sha256[32])
{
    static char buf[TRUST_MAX];
    char *line, *fp, *end;
    char text[100];
    load(buf, sizeof(buf));
    line = find_line(buf, trust_key);
    if (!line)
        return 0;
    fp = line + strlen(trust_key) + 1;
    end = fp + strcspn(fp, "\t\r\n");
    if ((size_t)(end - fp) >= sizeof(text))
        return 0;
    memcpy(text, fp, (size_t)(end - fp));
    text[end - fp] = 0;
    return oap_fingerprint_parse(text, sha256);
}

int oap_trust_remember(const char *trust_key, const char *fingerprint, const char *subject)
{
    static char buf[TRUST_MAX], out[TRUST_MAX];
    unsigned char check[32];
    char *line;
    const char *p;
    size_t used = 0;
    int n;
    if (!oap_fingerprint_parse(fingerprint, check) || strchr(trust_key, '\t') || strchr(trust_key, '\n'))
        return 0;
    load(buf, sizeof(buf));
    line = find_line(buf, trust_key);
    if (line) {                                /* replaces the printer's old one */
        char *next = strchr(line, '\n');
        memmove(line, next ? next + 1 : line + strlen(line), strlen(next ? next + 1 : line + strlen(line)) + 1);
    }
    used = strlen(buf);
    if (used && buf[used - 1] != '\n' && used + 1 < sizeof(buf))
        buf[used++] = '\n', buf[used] = 0;
    memcpy(out, buf, used);
    n = snprintf(out + used, sizeof(out) - used, "%s\t%s\t", trust_key, fingerprint);
    if (n < 0 || (size_t)n >= sizeof(out) - used)
        return 0;
    used += (size_t)n;
    for (p = subject; p && *p && used + 2 < sizeof(out); p++)
        out[used++] = (*p == '\t' || *p == '\n' || *p == '\r') ? ' ' : *p;
    out[used++] = '\n';
    return store(out, used);
}
