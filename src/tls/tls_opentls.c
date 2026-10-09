/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* The OpenTLS backend of the TLS abstraction (tls_backend.h):
 * opentls.library, API version 1 (DalsinAI/openamigatls,
 * docs/AutoDocs-OpenTLS.md; the headers are in third_party/opentls).
 *
 * Bytes move through an I/O hook (OT_SetIOHook) over the same socket, so
 * every wait stays bounded the way OpenPrint's plain connections are: the
 * hook waits up to the caller's time and answers OTERR_WOULDBLOCK when
 * nothing came. A remembered printer certificate is pinned
 * (OT_PinCertificate); otherwise the library checks the chain, the dates
 * and the name. Built only with OAP_TLS_OPENTLS; on x86 or ARM64 cores the
 * host tests link tests/host/opentls_host.c, the same calls on OpenSSL. */
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
#include <proto/exec.h>
#include <proto/opentls.h>
#include <clib/alib_protos.h>
struct Library *OpenTLSBase;
#else
#include <clib/opentls_protos.h>
#endif

/* A hook's entry, whose C type the NDK leaves open: through void (*)(void),
 * the one function type every function pointer may be cast to and from. */
#define HOOK_FN(f) ((ULONG (*)())(void (*)(void))(f))

typedef struct Session {
    struct OTConnection *conn;
    struct Hook hook;
    long fd;
    unsigned millis;                 /* how long the hook waits, this call */
} Session;

static struct OTContext *ctx;
static int opened;

static void ot_close(void)
{
    if (ctx)
        OT_FreeContext(ctx);
    ctx = NULL;
#ifdef __amigaos__
    if (OpenTLSBase)
        CloseLibrary(OpenTLSBase);
    OpenTLSBase = NULL;
#endif
    opened = 0;
}

static int ot_open(char *why, size_t cap)
{
    struct OTContextConfig config;
    char ca[256];
    LONG error = 0;
    if (opened)
        return 1;
#ifdef __amigaos__
    OpenTLSBase = OpenLibrary((CONST_STRPTR)OPENTLSLIB_NAME, OPENTLSLIB_VERSION);
    if (!OpenTLSBase) {
        snprintf(why, cap, "opentls.library isn't installed");
        return 0;
    }
#endif
    memset(&config, 0, sizeof(config));
    config.Size = sizeof(config);
    config.MinVersion = OT_TLS12;
    if (oap_tls_setting("TLS_CA_FILE", ca, sizeof(ca)))
        config.CAFile = (STRPTR)ca;
    ctx = OT_NewContext(&config, &error);
    if (!ctx) {
        snprintf(why, cap, "opentls.library: %s", (const char *)OT_ErrorString(error));
        ot_close();
        return 0;
    }
    opened = 1;
    return 1;
}

/* The I/O hook: A0 the hook, A2 the connection, A1 the message. */
static ULONG io_hook(struct Hook *hook, APTR connection, struct OTIOMessage *m)
{
    Session *s = hook->h_Data;
    long r;
    (void)connection;
    if (m->Length <= 0)
        return 0;
    if (m->MethodID == OTIO_READ)
        r = oap_sock_recv(s->fd, m->Buffer, (size_t)m->Length, s->millis);
    else if (m->MethodID == OTIO_WRITE)
        r = oap_sock_send(s->fd, m->Buffer, (size_t)m->Length, s->millis);
    else
        r = OAP_CONN_ERROR;
    if (r == OAP_CONN_TIMEOUT)
        return (ULONG)OTERR_WOULDBLOCK;
    if (r < 0)
        return (ULONG)OTERR_IO;
    return (ULONG)r;
}

static int verdict(LONG error)
{
    switch (error) {
    case OTERR_OK: return OAP_CERT_OK;
    case OTERR_UNTRUSTED: case OTERR_TRUSTSTORE: return OAP_CERT_ASK;
    case OTERR_HOSTNAME: return OAP_CERT_NAME;
    case OTERR_EXPIRED: return OAP_CERT_DATES;
    case OTERR_BADCERT: return OAP_CERT_BAD;
    case OTERR_PINNED: return OAP_CERT_CHANGED;
    default: return OAP_CERT_FAILED;
    }
}

static void ot_end(void *session)
{
    Session *s = session;
    if (!s)
        return;
    if (s->conn) {
        s->millis = 1000;
        OT_Close(s->conn);                     /* close_notify */
        OT_FreeConnection(s->conn);
    }
    free(s);
}

/* Who the certificate is for, from its DER. */
static void peer_names(Session *s, OAPTlsInfo *info)
{
    LONG len = OT_GetPeerCertificate(s->conn, NULL, 0);
    unsigned char *der;
    char until[16];
    if (len <= 0 || len > 65536 || !(der = malloc((size_t)len)))
        return;
    if (OT_GetPeerCertificate(s->conn, der, len) == len)
        oap_cert_der_names(der, (size_t)len, info->subject, sizeof(info->subject), until, sizeof(until));
    free(der);
}

static void *ot_start(long fd, const char *host, const unsigned char *pin, int mode,
                      OAPTlsInfo *info, char *err, size_t err_cap)
{
    Session *s = calloc(1, sizeof(*s));
    time_t end = time(NULL) + 30;              /* a 68k handshake takes a while */
    LONG error = 0, r;
    if (!s) {
        snprintf(err, err_cap, "Not enough memory for the encrypted connection");
        return NULL;
    }
    s->fd = fd;
    s->millis = 1000;
    s->conn = OT_NewConnection(ctx, (CONST_STRPTR)host, &error);
    if (!s->conn) {
        snprintf(err, err_cap, "Encrypted connection failed: %s", (const char *)OT_ErrorString(error));
        free(s);
        return NULL;
    }
#ifdef __amigaos__
    s->hook.h_Entry = HOOK_FN(HookEntry);          /* amiga.lib: registers to C arguments */
    s->hook.h_SubEntry = HOOK_FN(io_hook);
#else
    s->hook.h_Entry = HOOK_FN(io_hook);
#endif
    s->hook.h_Data = s;
    OT_SetIOHook(s->conn, &s->hook);
    OT_SetALPN(s->conn, (CONST_STRPTR)"http/1.1");
    if (mode == OAP_TLS_ANY)
        OT_SetVerify(s->conn, OTV_NONE);
    else if (pin)
        OT_PinCertificate(s->conn, pin);
    while ((r = OT_Handshake(s->conn)) == OTERR_WOULDBLOCK && time(NULL) <= end)
        ;
    if (OT_GetPeerFingerprint(s->conn, info->sha256) == OTERR_OK) {
        info->have_fingerprint = 1;
        peer_names(s, info);
    }
    if (r != OTERR_OK) {
        info->verdict = r == OTERR_WOULDBLOCK ? OAP_CERT_FAILED : verdict(r);
        oap_copy(info->problem, sizeof(info->problem), (const char *)OT_GetErrorText(s->conn));
        if (r == OTERR_WOULDBLOCK)
            snprintf(err, err_cap, "Encrypted connection timed out");
        else
            snprintf(err, err_cap, "Encrypted connection failed: %.150s", info->problem);
        OT_FreeConnection(s->conn);
        free(s);
        return NULL;
    }
    switch (OT_GetProtocol(s->conn)) {
    case OT_TLS13: oap_copy(info->protocol, sizeof(info->protocol), "TLSv1.3"); break;
    case OT_TLS12: oap_copy(info->protocol, sizeof(info->protocol), "TLSv1.2"); break;
    default: oap_copy(info->protocol, sizeof(info->protocol), "TLS"); break;
    }
    oap_copy(info->cipher, sizeof(info->cipher), (const char *)OT_GetCipher(s->conn));
    if (mode == OAP_TLS_ANY)
        info->verdict = OAP_CERT_ASK;           /* unchecked: the caller keeps the first try's verdict */
    else
        info->verdict = pin && info->have_fingerprint && !memcmp(pin, info->sha256, 32) ? OAP_CERT_TRUSTED : OAP_CERT_OK;
    return s;
}

static long ot_read(void *session, void *buf, size_t n, unsigned millis)
{
    Session *s = session;
    LONG r;
    s->millis = millis;
    r = OT_Read(s->conn, buf, (LONG)n);
    if (r >= 0)
        return r;
    if (r == OTERR_WOULDBLOCK)
        return OAP_CONN_TIMEOUT;
    if (r == OTERR_CLOSED)
        return 0;                              /* closed without close_notify: HTTP framing decides */
    return OAP_CONN_ERROR;
}

static long ot_write(void *session, const void *data, size_t n, unsigned millis)
{
    Session *s = session;
    LONG r;
    s->millis = millis;
    r = OT_Write(s->conn, (CONST_APTR)data, (LONG)n);
    if (r > 0)
        return r;
    return r == OTERR_WOULDBLOCK ? OAP_CONN_TIMEOUT : OAP_CONN_ERROR;
}

const OAPTlsBackend oap_tls_opentls = {
    "OpenTLS", 1, ot_open, ot_close, ot_start, ot_read, ot_write, ot_end
};
