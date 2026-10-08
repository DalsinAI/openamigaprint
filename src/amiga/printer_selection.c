/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* The printer chosen in Printers and Queue ("Use for printing"), for
 * OpenView: ENV:OpenPrint/PrinterURI, else its ENVARC: copy, else the
 * names used before 4 October 2026. */
#include "oap.h"
#include "oap_discovery.h"
#include <stdio.h>
#include <string.h>

int oap_selected_printer(char *uri, size_t cap)
{
    static const char *const places[] = {
        OAP_DISC_SELECTION, OAP_DISC_SAVED, OAP_DISC_SELECTION_OLD, OAP_DISC_SAVED_OLD
    };
    FILE *f = NULL;
    size_t i;
    for (i = 0; !f && i < sizeof(places) / sizeof(places[0]); i++)
        f = fopen(places[i], "r");
    if (!f)
        return 0;
    if (!fgets(uri, (int)cap, f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    uri[strcspn(uri, "\r\n")] = 0;
    return strlen(uri) > 6 && !strncmp(uri, "ipp://", 6);
}
