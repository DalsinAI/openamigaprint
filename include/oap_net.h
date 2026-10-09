/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#ifndef OAP_NET_H
#define OAP_NET_H
/* The one place OpenPrint meets a printer over TCP: a connection to the
 * host and port of an ipp:// address, with every wait bounded. Everything
 * above it (the IPP client, discovery's capability check, Print-Job) talks
 * to an OAPConn and never to a socket.
 *
 * On AmigaOS the sockets come from bsdsocket.library (Roadshow, AmiTCP,
 * Miami, Genesis, OpenSocket...); the library belongs to the task that
 * opens it, so each program that talks to printers calls oap_net_start()
 * first and oap_net_stop() at the end. On x86 or ARM64 cores (the host
 * tests) the same code uses BSD sockets. */
#include <stddef.h>
#include "oap.h"
#include "oap_discovery.h"

typedef struct OAPConn OAPConn;

int oap_net_start(void);                /* 1: bsdsocket.library is open */
void oap_net_stop(void);

/* Connects to the address's host and port within a few seconds. NULL on
 * failure, with the reason in `note`. */
OAPConn *oap_conn_open(const OAPUri *uri, char *note, size_t note_cap);
/* Sends all of it within `seconds`; 1 on success. */
int oap_conn_write(OAPConn *c, const void *data, size_t n, unsigned seconds);
/* Waits up to `millis` for data: the bytes read (>0), 0 at the end of the
 * stream, OAP_CONN_TIMEOUT when nothing came, OAP_CONN_ERROR on a failure. */
#define OAP_CONN_TIMEOUT (-2)
#define OAP_CONN_ERROR (-1)
long oap_conn_read(OAPConn *c, void *buf, size_t n, unsigned millis);
void oap_conn_close(OAPConn *c);

/* The HTTP request line and headers of an IPP POST of `length` bytes. */
int oap_http_post(OAPConn *c, const OAPUri *uri, unsigned long length);
/* Reads one complete HTTP response, within `seconds`, and gives its IPP
 * body. 1 on success; otherwise the reason is in `note`. */
int oap_receive_ipp(OAPConn *c, unsigned char *body, size_t cap, size_t *body_len,
                    unsigned seconds, char *note, size_t note_cap);
/* Get-Printer-Attributes: what the printer at `uri` can do, PDF above all.
 * 1 when it answered; `note` says what was found in words. */
int oap_query_pdf(const char *uri, OAPCaps *caps, char *note, size_t note_cap);
#endif
