/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* C:OAPDiscover: the network work of Printers and Queue, in a process of
 * its own. OAPDiscover OUTPUT searches the network; OAPDiscover --query
 * URI OUTPUT checks one printer by its address. Either writes what it
 * finds to OUTPUT as it goes (oap_discovery_save). */
#include "oap_stack.h"
#include "oap_discovery.h"
#include "oap_net.h"
#include "oap_str.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned long __stack = 65536;
static const char oap_version[] __attribute__((used)) = "$VER: OAPDiscover 0.4 (9.10.2026)";

static int query_one(const char *uri, const char *output)
{
    OAPDiscovery *d = calloc(1, sizeof(*d));
    OAPDiscovered *p;
    int ok;
    if (!d)
        return 20;
    d->count = 1;
    p = &d->printers[0];
    p->alive = 1;
    oap_copy(p->uri, sizeof(p->uri), uri);
    strcpy(p->label, "Manual printer");
    p->secure = !strncmp(uri, "ipps://", 7);
    if (!oap_net_start()) {
        oap_discovery_save(output, d, 1, "bsdsocket.library is not available");
        free(d);
        return 20;
    }
    ok = oap_query_found(p);
    oap_discovery_save(output, d, 1, p->note);
    printf("%s\n", p->note);
    oap_net_stop();
    free(d);
    return ok ? 0 : 10;
}

static int discover_main(int argc, char **argv)
{
    if (argc == 2)
        return oap_discover_run(argv[1]);
    if (argc == 4 && !strcmp(argv[1], "--query"))
        return query_one(argv[2], argv[3]);
    fprintf(stderr, "Usage: OAPDiscover OUTPUT | OAPDiscover --query ipp[s]://host:port/path OUTPUT\n");
    return 20;
}

int main(int argc, char **argv)
{
    return oap_main_with_stack(discover_main, argc, argv, 65536);
}
