/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* C:OAPDiscover's search: one-shot mDNS queries for IPP printers, then a
 * Get-Printer-Attributes check of each one found (oap_net.h). */
#if !defined(__amigaos__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif
#include "oap_discovery.h"
#include "oap_net.h"
#include "../net/oap_sock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int dns_send(long fd, const OAPName *name, unsigned type)
{
    unsigned char p[300];
    struct sockaddr_in sa;
    size_t n = oap_dns_query(name, type, 0x4f41, p, sizeof(p));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5353);
    sa.sin_addr.s_addr = inet_addr((OAP_SOCKSTR)"224.0.0.251");
    return n && sendto(fd, (OAP_SOCKBUF)p, n, 0, (struct sockaddr *)&sa, sizeof(sa)) == (long)n;
}

int oap_discover_run(const char *output)
{
    OAPDiscovery *d = calloc(1, sizeof(*d));
    OAPName name;
    long fd = -1;
    OAP_IOCTLARG nb = 1;
    unsigned phase, received = 0, parsed = 0, sent = 0;
    size_t i;
    struct sockaddr_in sa;
    static unsigned char packet[9000];
    char failure[256] = "Cannot open local-network discovery socket";
    if (!d)
        return 20;
    oap_discovery_save(output, d, 0, "Opening bsdsocket.library...");
    if (!oap_net_start()) {
        oap_discovery_save(output, d, 1, "bsdsocket.library is not available");
        free(d);
        return 20;
    }
    oap_discovery_save(output, d, 0, "Opening discovery UDP socket...");
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        goto fail;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = 0;
    sa.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0 || OAP_IOCTL(fd, FIONBIO, &nb) < 0)
        goto fail;
    oap_discovery_save(output, d, 0, "Discovering IPP printers on the local network...");
    /* One-shot mDNS, ephemeral source port: RFC 6762 section 6.7. */
#define SEND_DNS(n, t) do { \
        if (!dns_send(fd, (n), (t))) { \
            snprintf(failure, sizeof(failure), \
                     "Discovery send failed (error %d): check UDP mDNS access to 224.0.0.251:5353", OAP_SOCKERR()); \
            goto fail; \
        } \
        sent++; \
    } while (0)
    for (phase = 0; phase < 3; phase++) {
        time_t end = time(NULL) + 2;
        unsigned packets = 0;
        oap_dns_name_text("_ipp._tcp.local", &name);
        SEND_DNS(&name, 12);
        oap_dns_name_text("_ipps._tcp.local", &name);
        SEND_DNS(&name, 12);
        for (i = 0; i < d->count; i++) {
            OAPDiscovered *e = &d->printers[i];
            if (!e->have_srv)
                SEND_DNS(&e->service, 33);
            if (!e->have_txt)
                SEND_DNS(&e->service, 16);
            if (e->target.len && !e->ip[0])
                SEND_DNS(&e->target, 1);
        }
        while (time(NULL) < end && packets < 512) {
            long got;
            OAP_SOCKOPTLEN sl = sizeof(sa);
            if (!oap_sock_ready(fd, 0, 180))
                continue;
            got = recvfrom(fd, (OAP_SOCKBUF)packet, sizeof(packet), 0, (struct sockaddr *)&sa, &sl);
            if (got <= 0)
                continue;
            packets++;
            received++;
            if (sa.sin_port != htons(5353) || got < 12)
                continue;
            if ((packet[0] || packet[1]) && (packet[0] != 0x4f || packet[1] != 0x41))
                continue;
            if (oap_dns_packet(d, packet, (size_t)got)) {
                parsed++;
                oap_discovery_save(output, d, 0, "Resolving printer names, ports and resource paths...");
            }
        }
    }
#undef SEND_DNS
    oap_sock_close(fd);
    fd = -1;
    for (i = 0; i < d->count; i++) {
        OAPDiscovered *e = &d->printers[i];
        char msg[180];
        if (!e->alive)
            continue;
        if (!e->uri[0]) {
            strcpy(e->note, "Incomplete DNS-SD endpoint; cannot verify");
            continue;
        }
        snprintf(msg, sizeof(msg), "Checking PDF support: %.120s", e->label);
        strcpy(e->note, "Querying Get-Printer-Attributes...");
        oap_discovery_save(output, d, 0, msg);
        oap_query_pdf(e->uri, &e->caps, e->note, sizeof(e->note));
        oap_discovery_save(output, d, 0, msg);
    }
    {
        unsigned yes = 0;
        char msg[256];
        for (i = 0; i < d->count; i++)
            if (oap_pdf_eligible(&d->printers[i]))
                yes++;
        if (!received)
            snprintf(msg, sizeof(msg),
                     "No discovery replies (%u queries sent). Check printer power and LAN multicast access.", sent);
        else
            snprintf(msg, sizeof(msg), "Discovery complete: %lu endpoints, %u verified PDF; %u replies, %u parsed",
                     (unsigned long)d->count, yes, received, parsed);
        oap_discovery_save(output, d, 1, msg);
    }
    oap_net_stop();
    free(d);
    return 0;
fail:
    oap_sock_close(fd);
    oap_discovery_save(output, d, 1, failure);
    oap_net_stop();
    free(d);
    return 20;
}
