#include "oap.h"
#include <string.h>

void oap_job_defaults(OAPJobOptions *o)
{
    memset(o, 0, sizeof(*o));
    strcpy(o->printer_uri, "ipp://printer.local:631/ipp/print");
    strcpy(o->job_name, "OpenPrint Job");
    o->copies = 1;
    o->paper = OAP_PAPER_A4;
    o->orientation = OAP_PORTRAIT;
    o->color = OAP_COLOR;
    o->duplex = OAP_SIMPLEX;
}

/* As OAPSpooler tells a job's kind (src/spooler/oapspooler.c, kind()). */
const char *oap_document_format(const unsigned char *head, size_t n)
{
    if (n >= 5 && !memcmp(head, "%PDF-", 5))
        return OAP_FORMAT_PDF;
    if ((n >= 2 && !memcmp(head, "%!", 2)) || (n >= 3 && !memcmp(head, "\004%!", 3)))
        return OAP_FORMAT_PS;
    return NULL;
}

int oap_ps_title(const unsigned char *head, size_t n, char *out, size_t cap)
{
    size_t x = 0;
    if (!cap)
        return 0;
    out[0] = 0;
    while (x < n) {
        size_t end = x, a, z, k = 0;
        while (end < n && head[end] != '\n' && head[end] != '\r')
            end++;
        if (end - x >= 13 && !memcmp(head + x, "%%EndComments", 13))
            break;
        if (end - x > 8 && !memcmp(head + x, "%%Title:", 8)) {
            for (a = x + 8; a < end && (head[a] == ' ' || head[a] == '\t'); a++)
                ;
            for (z = end; z > a && (head[z - 1] == ' ' || head[z - 1] == '\t'); z--)
                ;
            if (z - a >= 2 && head[a] == '(' && head[z - 1] == ')')
                a++, z--;
            for (; a < z && k + 1 < cap; a++)
                out[k++] = head[a] < 32 || head[a] == 127 ? ' ' : (char)head[a];
            out[k] = 0;
            return k > 0;
        }
        x = end + 1;
    }
    return 0;
}
