/* Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT */
/* OpenPrefs' Printing page's own code against the Print window's (src/core/paper.c), on the host:
 *   - for every paper the page lists, its pr_put (openamigaprefs Printing/src/pr_prefs.c) writes printer.prefs as
 *     Use or Save does, from OpenPrinter's file and from printer.device's no-file state, and paper.c reads it back:
 *     the Print window starts on that paper (A4 for a size OpenPrint's driver prints on A4);
 *   - mutated files: paper.c takes a file exactly when the page's reader (Look/src/pf_iff.c pf_parse) does, and
 *     reads the paper the page shows (pr_get).
 * Built and run by tests/test_paper_page.sh from an openamigaprefs checkout. Files only; nothing is printed. */
#include "pf_iff.h"
#include "pr_prefs.h"
#include "oap_paper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); putchar('\n'); fails++; } } while (0)

/* the window's paper whose words begin with the page's name for it ("US Letter": "Letter ..."), or -1 */
static int window_paper(const char *page_name)
{
    int i;
    size_t n;
    if (!strncmp(page_name, "US ", 3))
        page_name += 3;
    n = strlen(page_name);
    for (i = 0; i < OAP_PAPERS; i++)
        if (!strncmp(oap_paper_label(i), page_name, n) && oap_paper_label(i)[n] == ' ')
            return i;
    return -1;
}

/* what the window starts on for the paper the page shows: the same size, or A4 where the driver prints on A4 */
static int want_for(int page_paper)
{
    return pr_paper_openprint(page_paper) ? window_paper(pr_paper_name(page_paper)) : OAP_PAPER_A4;
}

static uint8_t *bytes(const pf_file *f, long *n)
{
    uint8_t *b;
    *n = pf_size(f);
    b = malloc((size_t)*n);
    if (!b || pf_write(f, b, *n) != *n) { printf("FAIL: pf_write\n"); exit(1); }
    return b;
}

static void test_page_writes(void)
{
    int openprint, k, sizes = 0;
    for (k = 0; k < PR_PAPERS; k++)
        sizes += pr_paper_openprint(k);
    CHECK(sizes == OAP_PAPERS, "the page's sizes OpenPrint makes (%d) are the window's (%d)", sizes, OAP_PAPERS);
    for (openprint = 1; openprint >= 0; openprint--)
        for (k = 0; k < PR_PAPERS; k++) {
            pf_file t, g;
            pr_prefs p;
            long n;
            uint8_t *b;
            int paper = pr_paper_order[k], pt, want, kept = OAP_PAPER_A3, kept_page = OAP_PAGE_UNKNOWN;
            pf_init(&t);
            pf_init(&g);
            CHECK(pr_new_txt(&t, openprint) && pr_new_gfx(&g, openprint), "new files");
            pr_get(&t, &g, &p);
            p.paper = paper;                          /* chosen on the page, then Use or Save */
            CHECK(pr_put(&t, &g, &p), "put");
            b = bytes(&t, &n);
            pt = oap_printer_prefs_paper(b, (size_t)n);
            want = want_for(paper);
            CHECK(pt == paper, "%s: the page wrote %d, the window read %d", pr_paper_name(paper), paper, pt);
            CHECK(want >= 0 && oap_paper_from_page(pt) == want, "%s: the window's paper %d, not %d", pr_paper_name(paper),
                  oap_paper_from_page(pt), want);
            CHECK(oap_paper_start(-1, pt, &kept, &kept_page) == want, "%s: the Print window starts there",
                  pr_paper_name(paper));
            free(b);
            pf_free(&t);
            pf_free(&g);
        }
}

/* paper.c takes the same files the page does and reads the paper the page shows */
static void test_same_reader(void)
{
    pf_file t, g, back;
    pr_prefs p;
    long n;
    uint8_t *base, *b;
    unsigned long seed = 25102026UL;
    int i, taken = 0, differ = 0;
    pf_init(&t);
    pf_init(&g);
    pf_init(&back);
    pr_new_txt(&t, 1);
    base = bytes(&t, &n);
    b = malloc((size_t)n + 16);
    for (i = 0; i < 200000; i++) {
        long len = n, j, edits;
        int page_ok, mine, page_paper;
        memcpy(b, base, (size_t)n);
        seed = seed * 1103515245UL + 12345UL;
        edits = 1 + (long)((seed >> 16) % 4);
        for (j = 0; j < edits; j++) {
            long at;
            seed = seed * 1103515245UL + 12345UL;
            at = (long)((seed >> 8) % (unsigned long)n);
            switch ((seed >> 4) % 4) {
            case 0: b[at] = (uint8_t)(seed >> 24); break;                       /* any byte */
            case 1: b[at] ^= (uint8_t)(1u << ((seed >> 20) % 8)); break;        /* a bit */
            case 2: if (at >= 4 && at % 2 == 0) b[at - 1] = (uint8_t)((seed >> 24) & 0x7f); break;  /* a length */
            default: len = at < 12 ? 12 : at; break;                            /* cut short */
            }
        }
        page_ok = pf_parse(&back, b, len);
        mine = oap_printer_prefs_paper(b, (size_t)len);
        if (page_ok != (mine != OAP_PAGE_NONE)) {
            if (differ++ < 5) printf("FAIL: mutation %d: the page %s it, paper.c %s\n", i, page_ok ? "takes" : "refuses",
                                     mine != OAP_PAGE_NONE ? "takes" : "refuses");
            continue;
        }
        if (!page_ok) continue;
        taken++;
        pf_init(&g);
        pr_get(&back, &g, &p);                        /* what the page shows (pr_fix: an unknown size is A4) */
        page_paper = p.paper;
        if (oap_paper_from_page(mine) != want_for(page_paper)) {
            if (differ++ < 5) printf("FAIL: mutation %d: the page shows %s, the window starts on %d\n", i,
                                     pr_paper_name(page_paper), oap_paper_from_page(mine));
        }
        pf_free(&back);
    }
    checks++;
    if (differ) fails++;
    printf("%s: 200000 mutated printer.prefs files, %d taken by both, %d differences\n", differ ? "FAIL" : "ok", taken, differ);
    free(b);
    free(base);
    pf_free(&t);
}

int main(void)
{
    test_page_writes();
    test_same_reader();
    if (fails) {
        printf("FAIL: %d of %d checks of the Printing page against the Print window\n", fails, checks);
        return 1;
    }
    printf("PASS: %d checks: the Printing page's paper reaches the Print window\n", checks);
    return 0;
}
