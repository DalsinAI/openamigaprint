/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#ifndef OAP_TLS_BACKEND_H
#define OAP_TLS_BACKEND_H
/* Inside the TLS abstraction only: what each backend supplies. */
#include "oap_tls.h"

/* The second try of a LOOK connection, for a backend that cannot finish a
 * handshake it has refused (OpenTLS): no checks at all. Only src/net/conn.c
 * asks for it, and only after a LOOK connection was refused for its
 * certificate. */
#define OAP_TLS_ANY 2

typedef struct OAPTlsBackend {
    const char *name;
    /* After a refused certificate the connection is dead: a LOOK must
     * reconnect and try again with OAP_TLS_ANY. */
    int look_needs_reconnect;
    int (*open)(char *why, size_t cap);          /* 1: the library is open in this task */
    void (*close)(void);
    /* `pin`: the remembered fingerprint for this printer, or NULL. Sets
     * info->verdict (OAP_CERT_OK, _TRUSTED or a problem), the fingerprint
     * and what it can of the rest. VERIFY: NULL for any problem. LOOK:
     * goes on despite a certificate problem when it can. */
    void *(*start)(long fd, const char *host, const unsigned char *pin, int mode,
                   OAPTlsInfo *info, char *err, size_t err_cap);
    long (*read)(void *session, void *buf, size_t n, unsigned millis);
    long (*write)(void *session, const void *data, size_t n, unsigned millis);
    void (*end)(void *session);
} OAPTlsBackend;

#ifdef OAP_TLS_OPENTLS
extern const OAPTlsBackend oap_tls_opentls;
#endif
#ifdef OAP_TLS_OSSL
extern const OAPTlsBackend oap_tls_ossl;
#endif

/* 1 when the open backend cannot finish a LOOK at a refused certificate
 * (src/net/conn.c then reconnects and tries OAP_TLS_ANY). */
int oap_tls_look_reconnects(void);

/* Who a DER certificate is issued to (its subject's common name) and when
 * it ends ("2036-10-05"); 1 when it could be read. */
int oap_cert_der_names(const unsigned char *der, size_t len, char *subject, size_t subject_cap,
                       char *until, size_t until_cap);
/* Reads a setting for testing (ENV:OpenPrint/<name> on the Amiga, the
 * environment variable OAP_<NAME> elsewhere); 1 when set. */
int oap_tls_setting(const char *name, char *out, size_t cap);
#endif
