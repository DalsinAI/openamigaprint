/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#ifndef OAP_NET_H
#define OAP_NET_H
/* The one place OpenPrint meets a printer over TCP: a connection to the
 * host and port of an ipp:// or ipps:// address, with every wait bounded,
 * and TLS on the same socket for ipps:// (include/oap_tls.h). Everything
 * above it (the IPP client, discovery's capability check, Print-Job)
 * talks to an OAPConn and never to a socket or a TLS library.
 *
 * On AmigaOS the sockets come from bsdsocket.library (Roadshow, AmiTCP,
 * Miami, Genesis, OpenSocket...); the library belongs to the task that
 * opens it, so each program that talks to printers calls oap_net_start()
 * first and oap_net_stop() at the end. On x86 or ARM64 cores (the host
 * tests) the same code uses BSD sockets. */
#include <stddef.h>
#include "oap.h"
#include "oap_discovery.h"
#include "oap_tls.h"

typedef struct OAPConn OAPConn;

int oap_net_start(void);                /* 1: bsdsocket.library is open */
void oap_net_stop(void);

/* Connects to the address's host and port within a few seconds; for
 * ipps:// it then starts TLS (`tls_mode`: OAP_TLS_VERIFY or OAP_TLS_LOOK)
 * and `tls` (may be NULL) says what was found about the certificate. NULL
 * on failure, with the reason in `note` and tls->verdict. */
OAPConn *oap_conn_open(const OAPUri *uri, int tls_mode, OAPTlsInfo *tls, char *note, size_t note_cap);
/* 1 when the connection is TLS with a certificate that was accepted: the
 * only kind a password may be sent on. */
int oap_conn_trusted(const OAPConn *c);
/* Sends all of it within `seconds`; 1 on success. */
int oap_conn_write(OAPConn *c, const void *data, size_t n, unsigned seconds);
/* Waits up to `millis` for data: the bytes read (>0), 0 at the end of the
 * stream, OAP_CONN_TIMEOUT when nothing came, OAP_CONN_ERROR on a failure. */
#define OAP_CONN_TIMEOUT (-2)
#define OAP_CONN_ERROR (-1)
long oap_conn_read(OAPConn *c, void *buf, size_t n, unsigned millis);
void oap_conn_close(OAPConn *c);

/* The HTTP request line and headers of an IPP POST of `length` bytes;
 * `authorization` is a whole "Authorization: ...\r\n" line, or NULL. */
int oap_http_post(OAPConn *c, const OAPUri *uri, unsigned long length, const char *authorization);
typedef struct OAPHttpReply {
    int status;                     /* the HTTP status, 0 when none came */
    char challenge[400];            /* WWW-Authenticate, after a 401 */
} OAPHttpReply;
/* Reads one complete HTTP response, within `seconds`, and gives its IPP
 * body. 1 on success; otherwise the reason is in `note`, and `reply` (may
 * be NULL) has the status and any password challenge. */
int oap_receive_ipp(OAPConn *c, unsigned char *body, size_t cap, size_t *body_len,
                    unsigned seconds, OAPHttpReply *reply, char *note, size_t note_cap);
/* Get-Printer-Attributes: what the printer at `uri` can do, PDF above all.
 * 1 when it answered; `note` says what was found in words. For ipps://,
 * `tls` (may be NULL) says what was found about its certificate: the
 * question is asked even of a printer not yet trusted (OAP_TLS_LOOK), so
 * it can be shown and the user asked; nothing private is sent to it. */
int oap_query_pdf(const char *uri, OAPCaps *caps, OAPTlsInfo *tls, char *note, size_t note_cap);

/* After a 401: the Authorization line for a retry, when a password may be
 * sent (ipps://, an accepted certificate, a login kept for the printer);
 * otherwise 0 and `note` says why, in words. */
int oap_ipp_login(OAPConn *c, const OAPUri *uri, const OAPHttpReply *reply, unsigned counter,
                  char *authorization, size_t cap, char *note, size_t note_cap);

/* ---- passwords (src/net/http_auth.c): only ever on oap_conn_trusted() -- */
/* The Authorization line answering a Basic or Digest (MD5) challenge; 1
 * when it could be made. `counter` is Digest's nonce count (1, 2, ...). */
int oap_http_authorization(const char *challenge, const char *user, const char *password,
                           const char *method, const char *path, unsigned counter,
                           char *out, size_t cap);
/* The user name and password kept for a printer ("host:port") in
 * ENV:OpenPrint/Logins; 1 when there is one. */
int oap_login_lookup(const char *trust_key, char *user, size_t user_cap, char *password, size_t password_cap);
void oap_md5_hex(const void *data, size_t n, char out[33]);
extern const char *oap_digest_test_cnonce;   /* tests only: a fixed cnonce */

/* An IPv4 address for a .local name by one multicast DNS question, for
 * TCP/IP stacks that do not resolve .local themselves (src/net/mdns.c). */
int oap_mdns_resolve(const char *host, char *ip, size_t ip_cap);
#endif
