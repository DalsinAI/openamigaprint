/* Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT */
/* The paper the Print window starts on, and the sizes it offers (25 October 2026: the paper set on OpenPrefs'
 * Printing page didn't reach the Print window). Plain C, tested on the host by tests/test_paper.c.
 *
 * Where the Print window starts, first match wins (oap_paper_start):
 *   1. the document's own .job says a paper (paper=, as OpenView writes it);
 *   2. the paper chosen in the Print window itself, while the Printing page's paper is still the one it was
 *      chosen against (a choice the window sees the page's paper change after is dropped);
 *   3. the Printing page's paper (printer.prefs, pt_PaperSize; a size OpenPrint's driver prints on A4 is A4);
 *   4. with no printer.prefs at all: the paper last chosen in the Print window, else A4. */
#ifndef OAP_PAPER_H
#define OAP_PAPER_H
#include <stddef.h>
#include "oap.h"

/* printer.prefs' pt_PaperSize (prefs/printertxt.h), the sizes OpenPrint makes; the others it prints on A4 */
enum { OAP_PT_LETTER = 0, OAP_PT_LEGAL = 1, OAP_PT_A3 = 8, OAP_PT_A4 = 9, OAP_PT_A5 = 10 };

/* The Printing page's paper when there is none to read, and a remembered paper's page when it isn't known */
#define OAP_PAGE_NONE (-1)       /* no printer.prefs, or one that isn't a whole prefs file */
#define OAP_PAGE_UNKNOWN (-2)    /* remembered by an OpenPrint from before 25 October 2026, which didn't say */

int oap_paper_valid(int paper);                       /* 1 for an OAP_PAPER_ size */
const char *oap_paper_label(int paper);               /* the Print window's words: "A4 (210 x 297 mm)" (Latin-1 sign) */
void oap_paper_mm(int paper, int *w, int *h);         /* upright, in millimetres (the preview's shape) */
int oap_paper_from_page(int pt);                      /* pt_PaperSize -> OAP_PAPER_; one the driver prints on A4 -> A4 */

/* pt_PaperSize in the bytes of a printer.prefs file (IFF FORM PREF), as OpenPrefs' Printing page reads it: the
 * first PTXT chunk's, zero (US Letter) when it is too short or there is none; OAP_PAGE_NONE when the bytes aren't
 * a whole FORM PREF. */
int oap_printer_prefs_paper(const unsigned char *file, size_t len);

/* A .job's paper= value: OAP_PAPER_ size, or -1 for none (an empty, unknown or unreadable value) */
int oap_job_paper(const char *value);

/* The paper the Print window starts on (above). job: the .job's paper or -1; page: the Printing page's
 * pt_PaperSize or OAP_PAGE_NONE; *kept: the paper last chosen in the window or -1; *kept_page: the page's
 * pt_PaperSize it was chosen against (OAP_PAGE_NONE, OAP_PAGE_UNKNOWN). When the page's paper isn't that one any
 * more, the window's choice is dropped here (*kept -1, *kept_page the page's), so the next settings written don't
 * bring it back if the page returns to its old paper. */
int oap_paper_start(int job, int page, int *kept, int *kept_page);

/* The Print window's remembered settings (ENV:OpenPrint/PrintSettings), "copies paper layout colour sides page":
 * paper is the one last chosen in the window (-1 none), page the Printing page's pt_PaperSize it was chosen
 * against. A text with five numbers (written before 25 October 2026) has no page: OAP_PAGE_UNKNOWN.
 * read: 1 when the text holds settings; copies, layout, colour and sides go into o, the paper into *kept. */
int oap_settings_read(const char *text, OAPJobOptions *o, int *kept, int *kept_page);
void oap_settings_write(char *buf, size_t cap, const OAPJobOptions *o, int kept, int kept_page);

#endif
