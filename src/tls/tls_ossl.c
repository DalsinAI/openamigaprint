/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* The AmiSSL backend of the TLS abstraction (tls_backend.h): the OpenSSL 3
 * API, from AmiSSL 5 on AmigaOS (opened per task, over the task's own
 * bsdsocket.library) and from OpenSSL itself on x86 or ARM64 cores, where
 * the host tests run it against a printer emulator. Built only with
 * OAP_TLS_OSSL. */
#include "oap_tls.h"
#include "oap_str.h"
#include "tls_backend.h"
#include "../net/oap_sock.h"
#include "oap_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __amigaos__
#include <errno.h>
#include <proto/amisslmaster.h>
#include <proto/amissl.h>
#include <libraries/amisslmaster.h>
#include <libraries/amissl.h>
#include <amissl/amissl.h>
struct Library *AmiSSLMasterBase, *AmiSSLBase, *AmiSSLExtBase;
#endif
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/x509v3.h>

typedef struct Session {
    SSL *ssl;
    long fd;
} Session;

static SSL_CTX *ctx;

static void ossl_close(void)
{
    if (ctx)
        SSL_CTX_free(ctx);
    ctx = NULL;
#ifdef __amigaos__
    if (AmiSSLBase) {
        CloseAmiSSL();
        AmiSSLBase = AmiSSLExtBase = NULL;
    }
    if (AmiSSLMasterBase) {
        CloseLibrary(AmiSSLMasterBase);
        AmiSSLMasterBase = NULL;
    }
#endif
}

static int ossl_open(char *why, size_t cap)
{
    char ca[256];
    if (ctx)
        return 1;
#ifdef __amigaos__
    if (!SocketBase) {
        snprintf(why, cap, "The network is not open");
        return 0;
    }
    if (!(AmiSSLMasterBase = OpenLibrary((CONST_STRPTR)"amisslmaster.library", AMISSLMASTER_MIN_VERSION))) {
        snprintf(why, cap, "AmiSSL 5 isn't installed");
        return 0;
    }
    if (OpenAmiSSLTags(AMISSL_CURRENT_VERSION, AmiSSL_UsesOpenSSLStructs, FALSE,
                       AmiSSL_GetAmiSSLBase, (ULONG)&AmiSSLBase, AmiSSL_GetAmiSSLExtBase, (ULONG)&AmiSSLExtBase,
                       AmiSSL_SocketBase, (ULONG)SocketBase, AmiSSL_ErrNoPtr, (ULONG)&errno, TAG_DONE) != 0) {
        AmiSSLBase = AmiSSLExtBase = NULL;
        ossl_close();
        snprintf(why, cap, "AmiSSL 5 could not be opened");
        return 0;
    }
#endif
    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        ossl_close();
        snprintf(why, cap, "AmiSSL could not make a TLS context");
        return 0;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    /* The certificate is judged after the handshake (verdict()), so that a
     * printer's own certificate can be shown and trusted by its fingerprint. */
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    SSL_CTX_set_default_verify_paths(ctx);
    if (oap_tls_setting("TLS_CA_FILE", ca, sizeof(ca)))
        SSL_CTX_load_verify_locations(ctx, ca, NULL);
    return 1;
}

static int is_address(const char *host)
{
    const char *p;
    int dots = 0;
    for (p = host; *p; p++) {
        if (*p == '.')
            dots++;
        else if (*p < '0' || *p > '9')
            return 0;
    }
    return dots == 3;
}

/* Waits for what OpenSSL asked for; 0 when the time is up. */
static int wait_for(Session *s, int r, unsigned millis)
{
    int e = SSL_get_error(s->ssl, r);
    if (e == SSL_ERROR_WANT_READ)
        return oap_sock_ready(s->fd, 0, millis) ? 1 : 0;
    if (e == SSL_ERROR_WANT_WRITE)
        return oap_sock_ready(s->fd, 1, millis) ? 1 : 0;
    return -1;
}

static int verdict(long code)
{
    switch (code) {
    case X509_V_OK:
        return OAP_CERT_OK;
    case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
    case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
    case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
    case X509_V_ERR_CERT_UNTRUSTED:
        return OAP_CERT_ASK;
    case X509_V_ERR_HOSTNAME_MISMATCH:
    case X509_V_ERR_IP_ADDRESS_MISMATCH:
        return OAP_CERT_NAME;
    case X509_V_ERR_CERT_HAS_EXPIRED:
    case X509_V_ERR_CERT_NOT_YET_VALID:
        return OAP_CERT_DATES;
    default:
        return OAP_CERT_BAD;
    }
}

static void ossl_end(void *session)
{
    Session *s = session;
    if (!s)
        return;
    if (s->ssl) {
        SSL_shutdown(s->ssl);                  /* close_notify; the answer is not awaited */
        SSL_free(s->ssl);
    }
    free(s);
}

static void *ossl_start(long fd, const char *host, const unsigned char *pin, int mode,
                        OAPTlsInfo *info, char *err, size_t err_cap)
{
    Session *s = calloc(1, sizeof(*s));
    time_t end = time(NULL) + 20;
    X509 *cert;
    int r;
    if (!s || !(s->ssl = SSL_new(ctx))) {
        free(s);
        snprintf(err, err_cap, "Not enough memory for the encrypted connection");
        return NULL;
    }
    s->fd = fd;
    if (is_address(host))
        X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(s->ssl), host);
    else {
        SSL_set_tlsext_host_name(s->ssl, host);    /* SNI */
        SSL_set1_host(s->ssl, host);
    }
    SSL_set_fd(s->ssl, (int)fd);
    while ((r = SSL_connect(s->ssl)) != 1) {
        int w = wait_for(s, r, 1000);
        if (w < 0 || time(NULL) > end) {
            unsigned long e = ERR_get_error();
            info->verdict = OAP_CERT_FAILED;
            if (e)
                snprintf(err, err_cap, "Encrypted connection failed: %.120s", ERR_reason_error_string(e));
            else
                snprintf(err, err_cap, w < 0 ? "Encrypted connection failed" : "Encrypted connection timed out");
            ossl_end(s);
            return NULL;
        }
    }
    oap_copy(info->protocol, sizeof(info->protocol), SSL_get_version(s->ssl));
    oap_copy(info->cipher, sizeof(info->cipher), SSL_get_cipher_name(s->ssl));
    info->verdict = OAP_CERT_BAD;
    cert = SSL_get1_peer_certificate(s->ssl);
    if (cert) {
        unsigned int len = 0;
        unsigned char *der = NULL;
        int n;
        if (X509_digest(cert, EVP_sha256(), info->sha256, &len) && len == 32)
            info->have_fingerprint = 1;
        n = i2d_X509(cert, &der);
        if (n > 0) {
            char until[16];
            oap_cert_der_names(der, (size_t)n, info->subject, sizeof(info->subject), until, sizeof(until));
            OPENSSL_free(der);
        }
        X509_free(cert);
        if (pin && info->have_fingerprint && !memcmp(pin, info->sha256, 32))
            info->verdict = OAP_CERT_TRUSTED;
        else {
            long code = SSL_get_verify_result(s->ssl);
            info->verdict = verdict(code);
            if (code != X509_V_OK)
                snprintf(info->problem, sizeof(info->problem), "%s", X509_verify_cert_error_string(code));
        }
    }
    if (mode == OAP_TLS_VERIFY && !oap_cert_usable(info->verdict)) {
        ossl_end(s);
        return NULL;
    }
    return s;
}

static long ossl_read(void *session, void *buf, size_t n, unsigned millis)
{
    Session *s = session;
    time_t end = time(NULL) + (millis + 999) / 1000;
    for (;;) {
        int r = SSL_read(s->ssl, buf, (int)n), w;
        if (r > 0)
            return r;
        if (SSL_get_error(s->ssl, r) == SSL_ERROR_ZERO_RETURN)
            return 0;
        w = wait_for(s, r, millis);
        if (w < 0)
            return SSL_get_error(s->ssl, r) == SSL_ERROR_SYSCALL && !ERR_peek_error() ? 0 : OAP_CONN_ERROR;
        if (!w || time(NULL) > end)
            return OAP_CONN_TIMEOUT;
    }
}

static long ossl_write(void *session, const void *data, size_t n, unsigned millis)
{
    Session *s = session;
    for (;;) {
        int r = SSL_write(s->ssl, data, (int)n), w;
        if (r > 0)
            return r;
        w = wait_for(s, r, millis);
        if (w < 0)
            return OAP_CONN_ERROR;
        if (!w)
            return OAP_CONN_TIMEOUT;
    }
}

const OAPTlsBackend oap_tls_ossl = {
    "AmiSSL", 0, ossl_open, ossl_close, ossl_start, ossl_read, ossl_write, ossl_end
};
