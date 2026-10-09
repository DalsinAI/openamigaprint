/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* A .local name's IPv4 address by one multicast DNS question (RFC 6762
 * section 5.1, a one-shot query from an ephemeral port), for TCP/IP
 * stacks that do not resolve .local themselves. An ipps:// printer is
 * reached by its own name, because its certificate names it. */
#if !defined(__amigaos__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif
#include "oap_net.h"
#include "oap_sock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int oap_mdns_resolve(const char *host, char *ip, size_t ip_cap)
{
    OAPDiscovery *d;
    OAPName name;
    unsigned char query[300];
    static unsigned char packet[9000];
    struct sockaddr_in sa;
    size_t n, i;
    long fd;
    int tries, found = 0;
    OAP_IOCTLARG nb = 1;
    if (!oap_dns_name_text(host, &name) || !(n = oap_dns_query(&name, 1, 0x4f42, query, sizeof(query))))
        return 0;
    d = calloc(1, sizeof(*d));
    if (!d)
        return 0;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        free(d);
        return 0;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0 || OAP_IOCTL(fd, FIONBIO, &nb) < 0)
        goto out;
    for (tries = 0; tries < 2 && !found; tries++) {
        time_t end = time(NULL) + 1;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(5353);
        sa.sin_addr.s_addr = inet_addr((OAP_SOCKSTR)"224.0.0.251");
        if (sendto(fd, (OAP_SOCKBUF)query, n, 0, (struct sockaddr *)&sa, sizeof(sa)) != (long)n)
            break;
        while (!found && time(NULL) <= end) {
            OAP_SOCKOPTLEN sl = sizeof(sa);
            long got;
            if (!oap_sock_ready(fd, 0, 250))
                continue;
            got = recvfrom(fd, (OAP_SOCKBUF)packet, sizeof(packet), 0, (struct sockaddr *)&sa, &sl);
            if (got < 12 || sa.sin_port != htons(5353))
                continue;
            if (!oap_dns_packet(d, packet, (size_t)got))
                continue;
            for (i = 0; i < d->host_count; i++)
                if (d->hosts[i].ip[0] && d->hosts[i].name.len == name.len) {
                    size_t k;
                    for (k = 0; k < name.len; k++) {
                        unsigned char a = d->hosts[i].name.wire[k], b = name.wire[k];
                        if ((a | (a >= 'A' && a <= 'Z' ? 0x20 : 0)) != (b | (b >= 'A' && b <= 'Z' ? 0x20 : 0)))
                            break;
                    }
                    if (k == name.len) {
                        snprintf(ip, ip_cap, "%s", d->hosts[i].ip);
                        found = 1;
                        break;
                    }
                }
        }
    }
out:
    oap_sock_close(fd);
    free(d);
    return found;
}
