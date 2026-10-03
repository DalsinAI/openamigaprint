#include "oap.h"
#include <string.h>

void oap_job_defaults(OAPJobOptions *o)
{
    memset(o, 0, sizeof(*o));
    strcpy(o->printer_uri, "ipp://printer.local:631/ipp/print");
    strcpy(o->job_name, "OpenAmigaPrint Job");
    o->copies = 1;
    o->paper = OAP_PAPER_A4;
    o->orientation = OAP_PORTRAIT;
    o->color = OAP_COLOR;
    o->duplex = OAP_SIMPLEX;
}
