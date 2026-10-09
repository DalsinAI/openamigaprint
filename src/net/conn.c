/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* Connections to printers (include/oap_net.h): a non-blocking TCP socket
 * whose every wait is bounded by select()/WaitSelect(). */
#if !defined(__amigaos__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif
#include "oap_net.h"
#include "oap_sock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __amigaos__
struct Library *SocketBase;
#endif

struct OAPConn {
    long fd;
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
    if (!he || he->h_addrtype != AF_INET || he->h_length != 4 || !he->h_addr_list[0])
        return 0;
    memcpy(out, he->h_addr_list[0], 4);
    return 1;
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

OAPConn *oap_conn_open(const OAPUri *uri, char *note, size_t note_cap)
{
    OAPConn *c = calloc(1, sizeof(*c));
    if (!c) {
        snprintf(note, note_cap, "Not enough memory for the printer connection");
        return NULL;
    }
    c->fd = oap_sock_connect(uri->host, uri->port, 3500, note, note_cap);
    if (c->fd < 0) {
        free(c);
        return NULL;
    }
    return c;
}

int oap_conn_write(OAPConn *c, const void *data, size_t n, unsigned seconds)
{
    const unsigned char *p = data;
    time_t end = time(NULL) + seconds;
    while (n) {
        long sent;
        if (time(NULL) > end)
            return 0;
        sent = oap_sock_send(c->fd, p, n, 1000);
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
    return oap_sock_recv(c->fd, buf, n, millis);
}

void oap_conn_close(OAPConn *c)
{
    if (!c)
        return;
    oap_sock_close(c->fd);
    free(c);
}
