/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#ifndef OAP_SOCK_H
#define OAP_SOCK_H
/* Inside the network layer only: the socket calls of bsdsocket.library on
 * AmigaOS and of BSD sockets elsewhere, behind one set of names, and the
 * bounded socket helpers the connection and discovery code share. */
#include <stddef.h>

#ifdef __amigaos__
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
extern struct Library *SocketBase;
typedef STRPTR OAP_SOCKSTR;           /* the stack's calls take STRPTR names */
typedef APTR OAP_SOCKBUF;
typedef socklen_t OAP_SOCKOPTLEN;
typedef long OAP_IOCTLARG;
#define OAP_CLOSESOCK(s) CloseSocket(s)
#define OAP_IOCTL(s, r, p) IoctlSocket((s), (r), (char *)(p))
#define OAP_SELECT(n, r, w, e, t) WaitSelect((n), (r), (w), (e), (t), NULL)
#define OAP_SOCKERR() ((int)Errno())
#else
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
typedef char *OAP_SOCKSTR;
typedef void *OAP_SOCKBUF;
typedef socklen_t OAP_SOCKOPTLEN;
typedef int OAP_IOCTLARG;
#define OAP_CLOSESOCK(s) close((int)(s))
#define OAP_IOCTL(s, r, p) ioctl((int)(s), (r), (p))
#define OAP_SELECT(n, r, w, e, t) select((int)(n), (r), (w), (e), (t))
#define OAP_SOCKERR() errno
#endif

/* 1 when the socket can be read (or written) within `millis`. */
int oap_sock_ready(long fd, int for_write, unsigned millis);
/* A connected, non-blocking TCP socket, or -1 with the reason in `note`. */
long oap_sock_connect(const char *host, unsigned port, unsigned millis, char *note, size_t note_cap);
/* One send or receive, after waiting up to `millis`: the bytes moved, 0 at
 * the end of the stream (receive), OAP_CONN_TIMEOUT or OAP_CONN_ERROR. */
long oap_sock_send(long fd, const void *data, size_t n, unsigned millis);
long oap_sock_recv(long fd, void *buf, size_t n, unsigned millis);
void oap_sock_close(long fd);
#endif
