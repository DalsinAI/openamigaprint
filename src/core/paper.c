/* Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT */
/* The paper the Print window starts on, and the sizes it offers (include/oap_paper.h). Plain C: no Amiga calls,
 * so tests/test_paper.c checks it on the host; the Print window (src/ui/oap_print_requester.c) reads the files
 * and the variable and hands their bytes here. */
#include "oap_paper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ID(a, b, c, d) (((unsigned long)(a) << 24) | ((unsigned long)(b) << 16) | ((unsigned long)(c) << 8) | (unsigned long)(d))
#define ID_FORM ID('F', 'O', 'R', 'M')
#define ID_PREF ID('P', 'R', 'E', 'F')
#define ID_PTXT ID('P', 'T', 'X', 'T')
#define PTXT_PAPERSIZE 50        /* pt_PaperSize's offset in struct PrinterTxtPrefs (a UWORD, big-endian) */

static const char *const labels[OAP_PAPERS] = {
    "A4 (210 \xd7 297 mm)", "Letter (8.5 \xd7 11 in)", "Legal (8.5 \xd7 14 in)", "A3 (297 \xd7 420 mm)", "A5 (148 \xd7 210 mm)"
};
static const short mm[OAP_PAPERS][2] = { { 210, 297 }, { 216, 279 }, { 216, 356 }, { 297, 420 }, { 148, 210 } };

int oap_paper_valid(int paper) { return paper >= 0 && paper < OAP_PAPERS; }

const char *oap_paper_label(int paper) { return labels[oap_paper_valid(paper) ? paper : OAP_PAPER_A4]; }

void oap_paper_mm(int paper, int *w, int *h)
{
    if (!oap_paper_valid(paper))
        paper = OAP_PAPER_A4;
    *w = mm[paper][0];
    *h = mm[paper][1];
}

/* openprint_driver.c (geometry) makes A3, A4, A5, US Letter and US Legal pages; every other size it prints on A4,
 * and the Printing page says so. */
int oap_paper_from_page(int pt)
{
    switch (pt) {
    case OAP_PT_LETTER: return OAP_PAPER_LETTER;
    case OAP_PT_LEGAL: return OAP_PAPER_LEGAL;
    case OAP_PT_A3: return OAP_PAPER_A3;
    case OAP_PT_A5: return OAP_PAPER_A5;
    default: return OAP_PAPER_A4;
    }
}

static unsigned long be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

/* an IFF ID: four printable characters, the first not a space (as OpenPrefs' pf_parse takes them) */
static int good_id(unsigned long id)
{
    int k;
    if ((id >> 24) == ' ')
        return 0;
    for (k = 0; k < 4; k++) {
        unsigned long c = (id >> (24 - 8 * k)) & 0xff;
        if (c < 0x20 || c > 0x7e)
            return 0;
    }
    return 1;
}

/* The same file OpenPrefs' Printing page takes (openamigaprefs Look/src/pf_iff.c pf_parse): one FORM PREF, chunks
 * inside it and nothing nested; and the same paper (Printing/src/pr_prefs.c pr_get): a short PTXT reads as
 * zeros past its end, and with no PTXT the page shows printer.device's no-file state, whose size is zero. */
int oap_printer_prefs_paper(const unsigned char *b, size_t len)
{
    size_t end, i;
    unsigned long size;
    int pt = -1;
    if (!b || len < 12 || be32(b) != ID_FORM || be32(b + 8) != ID_PREF)
        return OAP_PAGE_NONE;
    size = be32(b + 4);
    if (size < 4 || size > len - 8)
        return OAP_PAGE_NONE;
    end = 8 + (size_t)size;
    for (i = 12; i < end;) {
        unsigned long id, n;
        if (end - i < 8)
            return OAP_PAGE_NONE;
        id = be32(b + i);
        n = be32(b + i + 4);
        if (!good_id(id) || id == ID_FORM || id == ID('L', 'I', 'S', 'T') || id == ID('C', 'A', 'T', ' ') ||
            id == ID('P', 'R', 'O', 'P') || n > end - i - 8)
            return OAP_PAGE_NONE;
        if (id == ID_PTXT && pt < 0) {
            const unsigned char *d = b + i + 8;
            pt = (n > PTXT_PAPERSIZE ? d[PTXT_PAPERSIZE] << 8 : 0) | (n > PTXT_PAPERSIZE + 1 ? d[PTXT_PAPERSIZE + 1] : 0);
        }
        i += 8 + (size_t)n + (size_t)(n & 1);
    }
    return pt >= 0 ? pt : 0;
}

int oap_job_paper(const char *value)
{
    char *end;
    long v;
    if (!value)
        return -1;
    v = strtol(value, &end, 10);
    if (end == value || v < 0 || v >= OAP_PAPERS)
        return -1;
    return (int)v;
}

int oap_paper_start(int job, int page, int *kept, int *kept_page)
{
    if (page >= 0 && *kept_page != page) {            /* the page's paper changed since: the window's choice goes */
        *kept = -1;
        *kept_page = page;
    }
    if (oap_paper_valid(job))
        return job;                                   /* the document's own */
    if (oap_paper_valid(*kept))
        return *kept;                                 /* chosen in the window, against the page's paper of now */
    return page >= 0 ? oap_paper_from_page(page) : OAP_PAPER_A4;
}

int oap_settings_read(const char *text, OAPJobOptions *o, int *kept, int *kept_page)
{
    int copies, paper, layout, colour, sides, page = OAP_PAGE_UNKNOWN, n;
    if (!text || (n = sscanf(text, "%d %d %d %d %d %d", &copies, &paper, &layout, &colour, &sides, &page)) < 5)
        return 0;
    if (copies >= 1 && copies <= 99)
        o->copies = copies;
    o->orientation = layout == OAP_LANDSCAPE ? OAP_LANDSCAPE : OAP_PORTRAIT;
    o->color = colour ? OAP_COLOR : OAP_MONO;
    o->duplex = sides >= OAP_SIMPLEX && sides <= OAP_DUPLEX_SHORT ? sides : OAP_SIMPLEX;
    *kept = oap_paper_valid(paper) ? paper : -1;
    *kept_page = n == 6 && page >= OAP_PAGE_NONE && page <= 0xffff ? page : OAP_PAGE_UNKNOWN;
    return 1;
}

void oap_settings_write(char *buf, size_t cap, const OAPJobOptions *o, int kept, int kept_page)
{
    snprintf(buf, cap, "%d %d %d %d %d %d", o->copies, oap_paper_valid(kept) ? kept : -1, o->orientation, o->color,
             o->duplex, kept_page);
}
