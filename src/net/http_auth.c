/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* HTTP Basic and Digest (RFC 7617, RFC 7616 with MD5) for printers that
 * ask for a user name and password (include/oap_net.h). OpenPrint answers
 * them only on ipps:// connections whose certificate was accepted: a
 * password never goes out in the clear, nor to a printer not yet trusted.
 *
 * The logins are ENV:OpenPrint/Logins (and its ENVARC: copy), one line
 * per printer, "host:port <TAB> user <TAB> password" (the host and port as
 * in the printer's address); OAP_LOGIN_FILE on x86 or ARM64 cores. */
#include "oap_net.h"
#include "oap_tls.h"
#include "oap_str.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __amigaos__
#include <exec/types.h>
#include <dos/var.h>
#include <proto/dos.h>
#endif

/* ---- MD5 (RFC 1321) ----------------------------------------------------- */

typedef struct MD5 {
    unsigned long a, b, c, d, bits_lo, bits_hi;
    unsigned char block[64];
    size_t used;
} MD5;

#define MD5_ROTL(x, n) ((((x) << (n)) | (((x) & 0xffffffffUL) >> (32 - (n)))) & 0xffffffffUL)

static void md5_block(MD5 *m, const unsigned char *p)
{
    static const unsigned long k[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
    };
    static const unsigned char r[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
    };
    unsigned long w[16], a = m->a, b = m->b, c = m->c, d = m->d;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = (unsigned long)p[i * 4] | (unsigned long)p[i * 4 + 1] << 8 |
               (unsigned long)p[i * 4 + 2] << 16 | (unsigned long)p[i * 4 + 3] << 24;
    for (i = 0; i < 64; i++) {
        unsigned long f, t;
        int g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) & 15;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) & 15;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) & 15;
        }
        t = d;
        d = c;
        c = b;
        b = (b + MD5_ROTL((a + f + k[i] + w[g]) & 0xffffffffUL, r[i])) & 0xffffffffUL;
        a = t;
    }
    m->a = (m->a + a) & 0xffffffffUL;
    m->b = (m->b + b) & 0xffffffffUL;
    m->c = (m->c + c) & 0xffffffffUL;
    m->d = (m->d + d) & 0xffffffffUL;
}

static void md5_init(MD5 *m)
{
    memset(m, 0, sizeof(*m));
    m->a = 0x67452301;
    m->b = 0xefcdab89;
    m->c = 0x98badcfe;
    m->d = 0x10325476;
}

static void md5_add(MD5 *m, const void *data, size_t n)
{
    const unsigned char *p = data;
    unsigned long lo = (m->bits_lo + ((unsigned long)n << 3)) & 0xffffffffUL;
    if (lo < m->bits_lo)
        m->bits_hi++;
    m->bits_hi += (unsigned long)n >> 29;
    m->bits_lo = lo;
    while (n--) {
        m->block[m->used++] = *p++;
        if (m->used == 64) {
            md5_block(m, m->block);
            m->used = 0;
        }
    }
}

/* The digest as 32 lower-case hex digits. */
static void md5_hex(MD5 *m, char out[33])
{
    static const char hex[] = "0123456789abcdef";
    unsigned char len[8], d[16];
    unsigned long words[4];
    int i;
    for (i = 0; i < 4; i++) {
        len[i] = (unsigned char)(m->bits_lo >> (8 * i));
        len[i + 4] = (unsigned char)(m->bits_hi >> (8 * i));
    }
    md5_add(m, "\x80", 1);
    while (m->used != 56)
        md5_add(m, "", 1);
    md5_add(m, len, 8);
    words[0] = m->a;
    words[1] = m->b;
    words[2] = m->c;
    words[3] = m->d;
    for (i = 0; i < 16; i++)
        d[i] = (unsigned char)(words[i / 4] >> (8 * (i % 4)));
    for (i = 0; i < 16; i++) {
        out[i * 2] = hex[d[i] >> 4];
        out[i * 2 + 1] = hex[d[i] & 15];
    }
    out[32] = 0;
}

void oap_md5_hex(const void *data, size_t n, char out[33])
{
    MD5 m;
    md5_init(&m);
    md5_add(&m, data, n);
    md5_hex(&m, out);
}

/* MD5 of "a:b:c" (or "a:b" when c is NULL). */
static void md5_join(const char *a, const char *b, const char *c, char out[33])
{
    MD5 m;
    md5_init(&m);
    md5_add(&m, a, strlen(a));
    md5_add(&m, ":", 1);
    md5_add(&m, b, strlen(b));
    if (c) {
        md5_add(&m, ":", 1);
        md5_add(&m, c, strlen(c));
    }
    md5_hex(&m, out);
}

/* ---- the challenge --------------------------------------------------- */

const char *oap_digest_test_cnonce;          /* tests only: a fixed cnonce */

static int base64(const char *in, char *out, size_t cap)
{
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t n = strlen(in), i, k = 0;
    if ((n + 2) / 3 * 4 + 1 > cap)
        return 0;
    for (i = 0; i < n; i += 3) {
        unsigned long v = (unsigned long)(unsigned char)in[i] << 16;
        if (i + 1 < n) v |= (unsigned long)(unsigned char)in[i + 1] << 8;
        if (i + 2 < n) v |= (unsigned char)in[i + 2];
        out[k++] = t[(v >> 18) & 63];
        out[k++] = t[(v >> 12) & 63];
        out[k++] = i + 1 < n ? t[(v >> 6) & 63] : '=';
        out[k++] = i + 2 < n ? t[v & 63] : '=';
    }
    out[k] = 0;
    return 1;
}

/* A parameter of a Digest challenge (realm="x", qop="auth,auth-int", ...). */
static int param(const char *challenge, const char *key, char *out, size_t cap)
{
    size_t kl = strlen(key);
    const char *p = challenge;
    while ((p = strstr(p, key)) != NULL) {
        int starts = p == challenge || p[-1] == ' ' || p[-1] == ',' || p[-1] == '\t';
        const char *v = p + kl;
        p += kl;
        while (*v == ' ')
            v++;
        if (!starts || *v != '=')
            continue;
        v++;
        while (*v == ' ')
            v++;
        if (*v == '"') {
            size_t k = 0;
            for (v++; *v && *v != '"' && k + 1 < cap; v++) {
                if (*v == '\\' && v[1])
                    v++;
                out[k++] = *v;
            }
            out[k] = 0;
        } else {
            size_t n = strcspn(v, ", \t");
            if (n >= cap)
                n = cap - 1;
            memcpy(out, v, n);
            out[n] = 0;
        }
        return 1;
    }
    return 0;
}

/* 1 when the comma-separated list holds `token`. */
static int has_token(const char *list, const char *token)
{
    size_t n = strlen(token);
    while (*list) {
        size_t k;
        while (*list == ' ' || *list == ',')
            list++;
        k = strcspn(list, ", ");
        if (k == n && !strncmp(list, token, n))
            return 1;
        list += k;
    }
    return 0;
}

static int starts_word(const char *s, const char *word)
{
    size_t n = strlen(word), i;
    while (*s == ' ')
        s++;
    for (i = 0; i < n; i++)
        if ((s[i] | 0x20) != (word[i] | 0x20))
            return 0;
    return s[n] == ' ' || !s[n];
}

int oap_http_authorization(const char *challenge, const char *user, const char *password,
                           const char *method, const char *path, unsigned counter,
                           char *out, size_t cap)
{
    if (starts_word(challenge, "Basic")) {
        char plain[256], coded[360];
        if ((size_t)snprintf(plain, sizeof(plain), "%s:%s", user, password) >= sizeof(plain) ||
            !base64(plain, coded, sizeof(coded)))
            return 0;
        return snprintf(out, cap, "Authorization: Basic %s\r\n", coded) < (int)cap;
    }
    if (starts_word(challenge, "Digest")) {
        char realm[128] = "", nonce[160] = "", opaque[160] = "", qop[64] = "", algorithm[32] = "MD5";
        char ha1[33], ha2[33], response[33], cnonce[17], nc[9], line[64];
        int auth_qop;
        size_t n;
        param(challenge, "realm", realm, sizeof(realm));
        if (!param(challenge, "nonce", nonce, sizeof(nonce)))
            return 0;
        param(challenge, "opaque", opaque, sizeof(opaque));
        param(challenge, "qop", qop, sizeof(qop));
        param(challenge, "algorithm", algorithm, sizeof(algorithm));
        if (strcmp(algorithm, "MD5") && strcmp(algorithm, "md5"))
            return 0;                          /* SHA-256 Digest: not yet */
        auth_qop = has_token(qop, "auth");
        if (qop[0] && !auth_qop)
            return 0;                          /* auth-int only: not offered */
        if (oap_digest_test_cnonce)
            oap_copy(cnonce, sizeof(cnonce), oap_digest_test_cnonce);
        else {
            static unsigned long calls;
            snprintf(line, sizeof(line), "%lu:%lu:%u:%s", (unsigned long)time(NULL), ++calls, counter, user);
            oap_md5_hex(line, strlen(line), ha1);
            memcpy(cnonce, ha1, 16);
            cnonce[16] = 0;
        }
        snprintf(nc, sizeof(nc), "%08x", counter);
        md5_join(user, realm, password, ha1);
        md5_join(method, path, NULL, ha2);
        if (auth_qop) {
            MD5 m;
            md5_init(&m);
            md5_add(&m, ha1, 32);
            md5_add(&m, ":", 1);
            md5_add(&m, nonce, strlen(nonce));
            md5_add(&m, ":", 1);
            md5_add(&m, nc, 8);
            md5_add(&m, ":", 1);
            md5_add(&m, cnonce, strlen(cnonce));
            md5_add(&m, ":auth:", 6);
            md5_add(&m, ha2, 32);
            md5_hex(&m, response);
        } else
            md5_join(ha1, nonce, ha2, response);
        n = (size_t)snprintf(out, cap, "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", "
                             "algorithm=MD5, response=\"%s\"", user, realm, nonce, path, response);
        if (n < cap && opaque[0])
            n += (size_t)snprintf(out + n, cap - n, ", opaque=\"%s\"", opaque);
        if (n < cap && auth_qop)
            n += (size_t)snprintf(out + n, cap - n, ", qop=auth, nc=%s, cnonce=\"%s\"", nc, cnonce);
        if (n < cap)
            n += (size_t)snprintf(out + n, cap - n, "\r\n");
        return n < cap;
    }
    return 0;
}

/* ---- the logins ------------------------------------------------------ */

int oap_login_lookup(const char *trust_key, char *user, size_t user_cap, char *password, size_t password_cap)
{
    static char buf[4096];
    size_t k = strlen(trust_key);
    char *line;
#ifdef __amigaos__
    LONG got = GetVar((STRPTR)"OpenPrint/Logins", (STRPTR)buf, sizeof(buf), GVF_GLOBAL_ONLY | GVF_BINARY_VAR);
    if (got <= 0)
        return 0;
    buf[got < (LONG)sizeof(buf) ? got : (LONG)sizeof(buf) - 1] = 0;
#else
    const char *path = getenv("OAP_LOGIN_FILE");
    FILE *f = path ? fopen(path, "rb") : NULL;
    size_t got;
    if (!f)
        return 0;
    got = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[got] = 0;
#endif
    for (line = buf; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL) {
        char *u, *pw, *end;
        if (strncmp(line, trust_key, k) || line[k] != '\t')
            continue;
        u = line + k + 1;
        pw = strchr(u, '\t');
        if (!pw)
            return 0;
        end = pw + 1 + strcspn(pw + 1, "\r\n");
        *pw++ = 0;
        *end = 0;
        oap_copy(user, user_cap, u);
        oap_copy(password, password_cap, pw);
        return user[0] != 0;
    }
    return 0;
}
