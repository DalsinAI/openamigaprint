/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* Connections to printers (include/oap_net.h): a non-blocking TCP socket
 * whose every wait is bounded by select()/WaitSelect(), with TLS on the
 * same socket for ipps:// (include/oap_tls.h). */
#if !defined(__amigaos__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif
#include "oap_net.h"
#include "oap_sock.h"
#include "../tls/tls_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __amigaos__
struct Library *SocketBase;
#endif

struct OAPConn {
    long fd;
    OAPTls *tls;                     /* ipps:// */
    int trusted;                     /* TLS with an accepted certificate */
};

int oap_net_start(void)
{
#ifdef __amigaos__
    if (!SocketBase)
        SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    return SocketBase != NULL;
#else
    return 1;
#endif
}

void oap_net_stop(void)
{
    oap_tls_stop();                  /* the TLS library uses the socket library */
#ifdef __amigaos__
    if (SocketBase) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
#endif
}

int oap_sock_ready(long fd, int for_write, unsigned millis)
{
    fd_set set;
    struct timeval tv;
    if (fd < 0 || fd >= FD_SETSIZE)
        return 0;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    tv.tv_sec = millis / 1000;
    tv.tv_usec = (millis % 1000) * 1000;
    return OAP_SELECT(fd + 1, for_write ? NULL : &set, for_write ? &set : NULL, NULL, &tv) > 0;
}

/* An IPv4 address for a dotted address or a name the stack can resolve. */
static int resolve(const char *host, struct in_addr *out)
{
    struct hostent *he;
    out->s_addr = inet_addr((OAP_SOCKSTR)host);
    if (out->s_addr != INADDR_NONE)
        return 1;
    he = gethostbyname((OAP_SOCKSTR)host);
    if (he && he->h_addrtype == AF_INET && he->h_length == 4 && he->h_addr_list[0]) {
        memcpy(out, he->h_addr_list[0], 4);
        return 1;
    }
    {
        /* a printer's own name (HP1234.local), which many stacks can't resolve */
        size_t n = strlen(host);
        char ip[16];
        if (n > 6 && !strcmp(host + n - 6, ".local") && oap_mdns_resolve(host, ip, sizeof(ip))) {
            out->s_addr = inet_addr((OAP_SOCKSTR)ip);
            return out->s_addr != INADDR_NONE;
        }
    }
    return 0;
}

long oap_sock_connect(const char *host, unsigned port, unsigned millis, char *note, size_t note_cap)
{
    struct sockaddr_in sa;
    long fd;
    int err = 0;
    OAP_SOCKOPTLEN elen = sizeof(err);
    OAP_IOCTLARG nb = 1;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    if (!resolve(host, &sa.sin_addr)) {
        snprintf(note, note_cap, "Can't find the printer %.120s on the network", host);
        return -1;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        snprintf(note, note_cap, "No socket for the printer connection");
        return -1;
    }
    if (OAP_IOCTL(fd, FIONBIO, &nb) < 0) {
        OAP_CLOSESOCK(fd);
        snprintf(note, note_cap, "No socket for the printer connection");
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0 &&
        (!oap_sock_ready(fd, 1, millis) || getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)&err, &elen) < 0 || err)) {
        OAP_CLOSESOCK(fd);
        snprintf(note, note_cap, "Printer connection failed or timed out");
        return -1;
    }
    return fd;
}

long oap_sock_send(long fd, const void *data, size_t n, unsigned millis)
{
    long sent;
    if (!oap_sock_ready(fd, 1, millis))
        return OAP_CONN_TIMEOUT;
    sent = send(fd, (OAP_SOCKBUF)data, n, 0);
    return sent > 0 ? sent : OAP_CONN_ERROR;
}

long oap_sock_recv(long fd, void *buf, size_t n, unsigned millis)
{
    long got;
    if (!oap_sock_ready(fd, 0, millis))
        return OAP_CONN_TIMEOUT;
    got = recv(fd, (OAP_SOCKBUF)buf, n, 0);
    return got >= 0 ? got : OAP_CONN_ERROR;
}

void oap_sock_close(long fd)
{
    if (fd >= 0)
        OAP_CLOSESOCK(fd);
}

static int cert_problem(int verdict)
{
    return verdict == OAP_CERT_ASK || verdict == OAP_CERT_NAME || verdict == OAP_CERT_DATES ||
           verdict == OAP_CERT_BAD || verdict == OAP_CERT_CHANGED;
}

OAPConn *oap_conn_open(const OAPUri *uri, int tls_mode, OAPTlsInfo *tls, char *note, size_t note_cap)
{
    OAPTlsInfo scratch;
    char key[300];
    OAPConn *c;
    if (!tls)
        tls = &scratch;
    memset(tls, 0, sizeof(*tls));
    tls->verdict = OAP_CERT_NONE;
    if (uri->secure && !oap_tls_available(note, note_cap)) {
        tls->verdict = OAP_CERT_NOTLS;         /* said before anything is sent */
        return NULL;
    }
    c = calloc(1, sizeof(*c));
    if (!c) {
        snprintf(note, note_cap, "Not enough memory for the printer connection");
        return NULL;
    }
    c->fd = oap_sock_connect(uri->host, uri->port, 3500, note, note_cap);
    if (c->fd < 0) {
        free(c);
        return NULL;
    }
    if (!uri->secure)
        return c;
    oap_trust_key(uri->host, uri->port, key, sizeof(key));
    c->tls = oap_tls_start(c->fd, uri->host, key, tls_mode, tls, note, note_cap);
    if (!c->tls && tls_mode == OAP_TLS_LOOK && cert_problem(tls->verdict) && oap_tls_look_reconnects()) {
        /* the backend ended the connection at the certificate: ask again,
         * unchecked, only to read what the printer can do */
        OAPTlsInfo second;
        char ignored[160];
        oap_sock_close(c->fd);
        c->fd = oap_sock_connect(uri->host, uri->port, 3500, note, note_cap);
        if (c->fd >= 0)
            c->tls = oap_tls_start(c->fd, uri->host, NULL, OAP_TLS_ANY, &second, ignored, sizeof(ignored));
        if (c->tls) {
            memcpy(tls->protocol, second.protocol, sizeof(tls->protocol));
            memcpy(tls->cipher, second.cipher, sizeof(tls->cipher));
            if (!tls->have_fingerprint && second.have_fingerprint) {
                /* no trust store at all (OTERR_TRUSTSTORE): the certificate
                 * was not read the first time; the unchecked look shows it */
                tls->have_fingerprint = 1;
                memcpy(tls->sha256, second.sha256, sizeof(tls->sha256));
                memcpy(tls->fingerprint, second.fingerprint, sizeof(tls->fingerprint));
                memcpy(tls->subject, second.subject, sizeof(tls->subject));
            }
        }
    }
    if (!c->tls) {
        oap_sock_close(c->fd);
        free(c);
        return NULL;
    }
    c->trusted = oap_cert_usable(tls->verdict);
    return c;
}

int oap_conn_trusted(const OAPConn *c)
{
    return c && c->tls && c->trusted;
}

int oap_conn_write(OAPConn *c, const void *data, size_t n, unsigned seconds)
{
    const unsigned char *p = data;
    time_t end = time(NULL) + seconds;
    while (n) {
        long sent;
        if (time(NULL) > end)
            return 0;
        sent = c->tls ? oap_tls_write(c->tls, p, n, 1000) : oap_sock_send(c->fd, p, n, 1000);
        if (sent == OAP_CONN_TIMEOUT)
            continue;
        if (sent <= 0)
            return 0;
        p += sent;
        n -= (size_t)sent;
    }
    return 1;
}

long oap_conn_read(OAPConn *c, void *buf, size_t n, unsigned millis)
{
    return c->tls ? oap_tls_read(c->tls, buf, n, millis) : oap_sock_recv(c->fd, buf, n, millis);
}

void oap_conn_close(OAPConn *c)
{
    if (!c)
        return;
    oap_tls_end(c->tls);
    oap_sock_close(c->fd);
    free(c);
}
