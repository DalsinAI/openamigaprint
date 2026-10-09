/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* Host tests only: the opentls.library calls OpenPrint's OpenTLS backend
 * uses (API version 1, docs/AutoDocs-OpenTLS.md in DalsinAI/openamigatls),
 * made on OpenSSL, so src/tls/tls_opentls.c runs on x86 or ARM64 cores
 * against a printer emulator while the library itself is being built.
 * It follows the contract as written: an I/O hook moves the bytes, the
 * certificate errors are OTERR_UNTRUSTED/_HOSTNAME/_EXPIRED/_BADCERT/
 * _PINNED, the fingerprint and DER stay readable after a certificate
 * failure, and a pinned certificate is accepted whatever its chain. It
 * is not opentls.library and never ships. */
#include <clib/opentls_protos.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/x509v3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct OTContext {
    SSL_CTX *ctx;
};

struct OTConnection {
    struct OTContext *context;
    SSL *ssl;
    BIO *in, *out;                  /* memory BIOs: the hook fills and drains them */
    struct Hook *hook;
    char host[256];
    LONG mode, error, state;        /* state: 0 new, 1 connected, 2 failed */
    int pinned;
    unsigned char pin[32];
    unsigned char *der;
    int der_len;
    unsigned char fp[32];
    int have_fp;
    char text[200];
};

typedef ULONG (*HookCall)(struct Hook *, APTR, struct OTIOMessage *);

static LONG hook_io(struct OTConnection *c, ULONG method, APTR buf, LONG len)
{
    struct OTIOMessage m;
    m.MethodID = method;
    m.Buffer = buf;
    m.Length = len;
    return (LONG)((HookCall)c->hook->h_Entry)(c->hook, c, &m);
}

ULONG OT_Version(VOID) { return (1UL << 16) | 0; }

struct OTContext *OT_NewContext(CONST struct OTContextConfig *config, LONG *error)
{
    struct OTContext *x = calloc(1, sizeof(*x));
    if (!x || !(x->ctx = SSL_CTX_new(TLS_client_method()))) {
        free(x);
        if (error)
            *error = OTERR_NOMEM;
        return NULL;
    }
    SSL_CTX_set_min_proto_version(x->ctx, config && config->MinVersion == OT_TLS13 ? TLS1_3_VERSION : TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(x->ctx, TLS1_2_VERSION);       /* version 1 speaks TLS 1.2 */
    SSL_CTX_set_verify(x->ctx, SSL_VERIFY_NONE, NULL);
    SSL_CTX_set_default_verify_paths(x->ctx);
    if (config && config->CAFile)
        SSL_CTX_load_verify_locations(x->ctx, (const char *)config->CAFile, NULL);
    return x;
}

VOID OT_FreeContext(struct OTContext *context)
{
    if (context) {
        SSL_CTX_free(context->ctx);
        free(context);
    }
}

struct OTConnection *OT_NewConnection(struct OTContext *context, CONST_STRPTR hostname, LONG *error)
{
    struct OTConnection *c = calloc(1, sizeof(*c));
    if (!c || !context || !hostname) {
        free(c);
        if (error)
            *error = OTERR_ARGS;
        return NULL;
    }
    c->context = context;
    snprintf(c->host, sizeof(c->host), "%s", (const char *)hostname);
    return c;
}

VOID OT_FreeConnection(struct OTConnection *c)
{
    if (!c)
        return;
    if (c->ssl)
        SSL_free(c->ssl);                     /* frees the BIOs too */
    free(c->der);
    free(c);
}

LONG OT_SetSocket(struct OTConnection *c, LONG s, struct Library *b) { (void)c; (void)s; (void)b; return OTERR_UNSUPPORTED; }
LONG OT_SetIOHook(struct OTConnection *c, struct Hook *hook) { if (!c || !hook) return OTERR_ARGS; c->hook = hook; return OTERR_OK; }
LONG OT_SetALPN(struct OTConnection *c, CONST_STRPTR p) { (void)c; (void)p; return OTERR_OK; }
LONG OT_SetVerify(struct OTConnection *c, LONG mode) { if (!c || mode < 0 || mode > 3) return OTERR_ARGS; c->mode = mode; return OTERR_OK; }

LONG OT_PinCertificate(struct OTConnection *c, CONST UBYTE *sha256)
{
    if (!c)
        return OTERR_ARGS;
    c->pinned = sha256 != NULL;
    if (sha256)
        memcpy(c->pin, sha256, 32);
    return OTERR_OK;
}

/* Sends what OpenSSL wrote; 1 when all went, 0 to call again, <0 on error. */
static LONG flush_out(struct OTConnection *c)
{
    char buf[4096];
    int n;
    while ((n = BIO_read(c->out, buf, sizeof(buf))) > 0) {
        int off = 0;
        while (off < n) {
            LONG w = hook_io(c, OTIO_WRITE, buf + off, n - off);
            if (w == OTERR_WOULDBLOCK)
                continue;                     /* hold on to it: the rest must follow */
            if (w <= 0)
                return OTERR_IO;
            off += w;
        }
    }
    return 1;
}

/* Reads from the hook into OpenSSL; 1 read, 0 end of stream, or an error. */
static LONG fill_in(struct OTConnection *c)
{
    char buf[4096];
    LONG r = hook_io(c, OTIO_READ, buf, sizeof(buf));
    if (r > 0)
        BIO_write(c->in, buf, (int)r);
    return r;
}

static LONG fail(struct OTConnection *c, LONG error, const char *text)
{
    c->error = error;
    c->state = 2;
    snprintf(c->text, sizeof(c->text), "%s", text);
    return error;
}

static LONG judge(struct OTConnection *c)
{
    X509 *cert = SSL_get1_peer_certificate(c->ssl);
    long code;
    unsigned int len = 0;
    if (!cert)
        return fail(c, OTERR_BADCERT, "The server sent no certificate");
    free(c->der);
    c->der = NULL;
    c->der_len = i2d_X509(cert, NULL);
    if (c->der_len > 0 && (c->der = malloc((size_t)c->der_len))) {
        unsigned char *p = c->der;
        i2d_X509(cert, &p);
    }
    c->have_fp = X509_digest(cert, EVP_sha256(), c->fp, &len) && len == 32;
    X509_free(cert);
    if (c->mode == OTV_NONE)
        return OTERR_OK;
    if (c->pinned && c->have_fp && !memcmp(c->pin, c->fp, 32))
        return OTERR_OK;
    if (c->mode == OTV_PINNED_ONLY)
        return fail(c, OTERR_PINNED, "The certificate is not the pinned one");
    code = SSL_get_verify_result(c->ssl);
    switch (code) {
    case X509_V_OK:
        return OTERR_OK;
    case X509_V_ERR_HOSTNAME_MISMATCH:
    case X509_V_ERR_IP_ADDRESS_MISMATCH:
        return c->mode == OTV_NO_HOSTNAME ? OTERR_OK : fail(c, OTERR_HOSTNAME, "The certificate is not for this host");
    case X509_V_ERR_CERT_HAS_EXPIRED:
    case X509_V_ERR_CERT_NOT_YET_VALID:
        return fail(c, OTERR_EXPIRED, "The certificate is expired or not yet valid");
    case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
    case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
    case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
        return fail(c, OTERR_UNTRUSTED, "The certificate is not signed by a trusted authority");
    default:
        return fail(c, OTERR_BADCERT, X509_verify_cert_error_string(code));
    }
}

static int is_address(const char *h)
{
    int dots = 0;
    for (; *h; h++) {
        if (*h == '.')
            dots++;
        else if (*h < '0' || *h > '9')
            return 0;
    }
    return dots == 3;
}

LONG OT_Handshake(struct OTConnection *c)
{
    int r;
    if (!c || !c->hook)
        return OTERR_ARGS;
    if (c->state == 1)
        return OTERR_OK;
    if (c->state == 2)
        return OTERR_STATE;
    if (!c->ssl) {
        if (!(c->ssl = SSL_new(c->context->ctx)))
            return fail(c, OTERR_NOMEM, "Out of memory");
        c->in = BIO_new(BIO_s_mem());
        c->out = BIO_new(BIO_s_mem());
        BIO_set_mem_eof_return(c->in, -1);
        SSL_set_bio(c->ssl, c->in, c->out);
        if (is_address(c->host))
            X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(c->ssl), c->host);
        else {
            SSL_set_tlsext_host_name(c->ssl, c->host);
            SSL_set1_host(c->ssl, c->host);
        }
        SSL_set_connect_state(c->ssl);
    }
    for (;;) {
        LONG got;
        r = SSL_do_handshake(c->ssl);
        if (flush_out(c) < 0)
            return fail(c, OTERR_IO, "The I/O hook failed");
        if (r == 1) {
            LONG v = judge(c);
            if (v == OTERR_OK)
                c->state = 1;
            return v;
        }
        if (SSL_get_error(c->ssl, r) != SSL_ERROR_WANT_READ)
            return fail(c, OTERR_PROTOCOL, "The TLS handshake failed");
        got = fill_in(c);
        if (got == OTERR_WOULDBLOCK)
            return OTERR_WOULDBLOCK;
        if (got == 0)
            return fail(c, OTERR_CLOSED, "The server closed the connection");
        if (got < 0)
            return fail(c, OTERR_IO, "The I/O hook failed");
    }
}

LONG OT_Read(struct OTConnection *c, APTR buffer, LONG length)
{
    if (!c || !buffer || length < 0)
        return OTERR_ARGS;
    if (c->state != 1)
        return OTERR_STATE;
    for (;;) {
        int r = SSL_read(c->ssl, buffer, (int)length);
        LONG got;
        if (r > 0)
            return r;
        if (SSL_get_error(c->ssl, r) == SSL_ERROR_ZERO_RETURN)
            return 0;
        if (SSL_get_error(c->ssl, r) != SSL_ERROR_WANT_READ)
            return OTERR_PROTOCOL;
        got = fill_in(c);
        if (got == 0)
            return OTERR_CLOSED;
        if (got < 0)
            return got == OTERR_WOULDBLOCK ? OTERR_WOULDBLOCK : OTERR_IO;
    }
}

LONG OT_Write(struct OTConnection *c, CONST_APTR buffer, LONG length)
{
    int r;
    if (!c || !buffer || length < 0)
        return OTERR_ARGS;
    if (c->state != 1)
        return OTERR_STATE;
    r = SSL_write(c->ssl, buffer, (int)length);
    if (r <= 0)
        return OTERR_PROTOCOL;
    return flush_out(c) < 0 ? OTERR_IO : r;
}

LONG OT_Pending(struct OTConnection *c) { return c && c->ssl ? SSL_pending(c->ssl) : 0; }

LONG OT_Close(struct OTConnection *c)
{
    if (!c || c->state != 1)
        return OTERR_STATE;
    SSL_shutdown(c->ssl);
    return flush_out(c) < 0 ? OTERR_IO : OTERR_OK;
}

LONG OT_GetError(struct OTConnection *c) { return c ? c->error : OTERR_ARGS; }
CONST_STRPTR OT_GetErrorText(struct OTConnection *c) { return (CONST_STRPTR)(c ? c->text : ""); }
LONG OT_GetErrorDetail(struct OTConnection *c) { (void)c; return 0; }
CONST_STRPTR OT_ErrorString(LONG e) { static char t[32]; snprintf(t, sizeof(t), "OpenTLS error %ld", (long)e); return (CONST_STRPTR)t; }

LONG OT_GetProtocol(struct OTConnection *c)
{
    int v = c && c->ssl ? SSL_version(c->ssl) : 0;
    return v == TLS1_3_VERSION ? OT_TLS13 : v == TLS1_2_VERSION ? OT_TLS12 : 0;
}

CONST_STRPTR OT_GetCipher(struct OTConnection *c) { return (CONST_STRPTR)(c && c->ssl ? SSL_get_cipher_name(c->ssl) : ""); }
CONST_STRPTR OT_GetALPN(struct OTConnection *c) { (void)c; return (CONST_STRPTR)""; }

LONG OT_GetPeerFingerprint(struct OTConnection *c, UBYTE *sha256)
{
    if (!c || !sha256)
        return OTERR_ARGS;
    if (!c->have_fp)
        return OTERR_STATE;
    memcpy(sha256, c->fp, 32);
    return OTERR_OK;
}

LONG OT_GetPeerCertificate(struct OTConnection *c, APTR buffer, LONG length)
{
    if (!c)
        return OTERR_ARGS;
    if (!c->der)
        return OTERR_STATE;
    if (!buffer)
        return c->der_len;
    if (length < c->der_len)
        return OTERR_ARGS;
    memcpy(buffer, c->der, (size_t)c->der_len);
    return c->der_len;
}
