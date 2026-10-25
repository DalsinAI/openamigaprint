/* Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT */
/* Host checks for where the Print window's paper starts and the sizes it offers (src/core/paper.c), and their IPP
 * media names (src/core/ipp.c). Each paper's Print-Job request start is written to build/paper-<media>.ipp: files
 * only, nothing is sent anywhere.
 *   make test-paper */
#include "oap.h"
#include "oap_paper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); putchar('\n'); fails++; } } while (0)

/* The printer.prefs openamigaup's OpenPrinter part writes where there is none (tools/build_pack.py printer_prefs;
 * the same bytes as openamigaprefs Printing/tests/openprinter-printer.prefs): PRHD, PTXT (OpenPrint, A4), PUNT
 * (oapspool), PDEV. OpenPrefs' Printing page changes pt_PaperSize in place: PTXT's data is at 34, the size at 84. */
static const unsigned char openprinter[222] = {
    0x46,0x4f,0x52,0x4d,0x00,0x00,0x00,0xd6,0x50,0x52,0x45,0x46,0x50,0x52,0x48,0x44,0x00,0x00,0x00,0x06,
    0x00,0x00,0x00,0x00,0x00,0x00,0x50,0x54,0x58,0x54,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x4f,0x70,0x65,0x6e,0x50,0x72,0x69,0x6e,0x74,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x01,0x00,0x09,0x00,0x42,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x4b,0x00,0x01,0x50,0x55,
    0x4e,0x54,0x00,0x00,0x00,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x6f,0x61,0x70,0x73,0x70,0x6f,0x6f,0x6c,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x50,0x44,0x45,0x56,0x00,0x00,0x00,0x34,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x4f,0x70,0x65,0x6e,0x50,0x72,0x69,0x6e,0x74,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00
};
#define PAPERSIZE_AT 84

static void put32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}

/* the OpenPrinter file with pt_PaperSize set, as the Printing page's Use or Save leaves it */
static const unsigned char *with_paper(int pt)
{
    static unsigned char b[sizeof openprinter];
    memcpy(b, openprinter, sizeof b);
    b[PAPERSIZE_AT] = (unsigned char)(pt >> 8);
    b[PAPERSIZE_AT + 1] = (unsigned char)pt;
    return b;
}

static void test_page_mapping(void)
{
    /* pt_PaperSize: US_LETTER 0, US_LEGAL 1, N_TRACTOR 2, W_TRACTOR 3, CUSTOM 4, A0 5 ... A8 13 */
    static const int want[14] = { OAP_PAPER_LETTER, OAP_PAPER_LEGAL, OAP_PAPER_A4, OAP_PAPER_A4, OAP_PAPER_A4,
                                  OAP_PAPER_A4, OAP_PAPER_A4, OAP_PAPER_A4, OAP_PAPER_A3, OAP_PAPER_A4, OAP_PAPER_A5,
                                  OAP_PAPER_A4, OAP_PAPER_A4, OAP_PAPER_A4 };
    int pt, n = 0;
    for (pt = 0; pt < 14; pt++) {
        CHECK(oap_paper_from_page(pt) == want[pt], "page size %d: paper %d, not %d", pt, oap_paper_from_page(pt), want[pt]);
        n += pt == OAP_PT_LETTER || pt == OAP_PT_LEGAL || pt == OAP_PT_A3 || pt == OAP_PT_A4 || pt == OAP_PT_A5;
    }
    CHECK(n == OAP_PAPERS, "the five sizes the driver makes are the window's five");
    CHECK(oap_paper_from_page(-1) == OAP_PAPER_A4 && oap_paper_from_page(999) == OAP_PAPER_A4, "out of range: A4");
}

/* Each size's words, shape and IPP media name; the shape against the driver's MediaBox (openprint_driver.c,
 * geometry: points) to within 2 points, so the preview is the page the driver makes. */
static void test_sizes(void)
{
    static const struct { int paper; const char *word, *media; int pt_w, pt_h; } t[OAP_PAPERS] = {
        { OAP_PAPER_A4, "A4 ", "iso_a4_210x297mm", 595, 842 },
        { OAP_PAPER_LETTER, "Letter ", "na_letter_8.5x11in", 612, 792 },
        { OAP_PAPER_LEGAL, "Legal ", "na_legal_8.5x14in", 612, 1008 },
        { OAP_PAPER_A3, "A3 ", "iso_a3_297x420mm", 842, 1191 },
        { OAP_PAPER_A5, "A5 ", "iso_a5_148x210mm", 420, 595 },
    };
    int i, w, h;
    for (i = 0; i < OAP_PAPERS; i++) {
        long pw, ph;
        CHECK(t[i].paper == i, "the table in OAP_PAPER_ order");
        CHECK(oap_paper_valid(i), "paper %d valid", i);
        CHECK(!strncmp(oap_paper_label(i), t[i].word, strlen(t[i].word)), "label %d: %s", i, oap_paper_label(i));
        CHECK(!strcmp(oap_ipp_media(i), t[i].media), "media %d: %s", i, oap_ipp_media(i));
        oap_paper_mm(i, &w, &h);
        pw = (w * 7200L + 1270) / 2540;
        ph = (h * 7200L + 1270) / 2540;
        CHECK(labs(pw - t[i].pt_w) <= 2 && labs(ph - t[i].pt_h) <= 2, "%s: %d x %d mm is %ld x %ld points, the driver's %d x %d",
              t[i].media, w, h, pw, ph, t[i].pt_w, t[i].pt_h);
    }
    CHECK(!oap_paper_valid(-1) && !oap_paper_valid(OAP_PAPERS), "out of range: not valid");
    CHECK(!strcmp(oap_ipp_media(-1), "iso_a4_210x297mm") && !strcmp(oap_ipp_media(OAP_PAPERS), "iso_a4_210x297mm"),
          "an unknown paper's media: A4's");
    oap_paper_mm(9, &w, &h);
    CHECK(w == 210 && h == 297 && !strncmp(oap_paper_label(9), "A4 ", 3), "an unknown paper's shape and words: A4's");
}

/* The IPP Print-Job request's start, for each paper: its media attribute (0x44 keyword "media") holds the name.
 * Written to build/ as files; nothing is sent. */
static void test_ipp(void)
{
    int i;
    for (i = 0; i < OAP_PAPERS; i++) {
        OAPJobOptions o;
        unsigned char b[2048], want[64];
        size_t n = 0, m, k;
        char path[96];
        FILE *f;
        int found = 0;
        const char *media = oap_ipp_media(i);
        oap_job_defaults(&o);
        strcpy(o.printer_uri, "ipp://printer.invalid/ipp/print");
        o.paper = i;
        CHECK(oap_ipp_build_prefix(&o, b, sizeof b, &n), "build the request for %s", media);
        /* tag, name length, "media", value length, value */
        m = 0;
        want[m++] = 0x44; want[m++] = 0; want[m++] = 5; memcpy(want + m, "media", 5); m += 5;
        want[m++] = 0; want[m++] = (unsigned char)strlen(media); memcpy(want + m, media, strlen(media)); m += strlen(media);
        for (k = 0; k + m <= n; k++)
            if (!memcmp(b + k, want, m)) found++;
        CHECK(found == 1, "%s: one media attribute with it (%d)", media, found);
        snprintf(path, sizeof path, "build/paper-%s.ipp", media);
        f = fopen(path, "wb");
        CHECK(f && fwrite(b, 1, n, f) == n && !fclose(f), "write %s", path);
    }
}

static void test_prefs_file(void)
{
    unsigned char b[sizeof openprinter + 64];
    static const int sizes[] = { OAP_PT_LETTER, OAP_PT_LEGAL, OAP_PT_A3, OAP_PT_A4, OAP_PT_A5, 2, 5, 13 };
    size_t i;

    CHECK(oap_printer_prefs_paper(openprinter, sizeof openprinter) == OAP_PT_A4, "OpenPrinter's printer.prefs: A4");
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
        CHECK(oap_printer_prefs_paper(with_paper(sizes[i]), sizeof openprinter) == sizes[i], "the page's size %d read", sizes[i]);
    CHECK(oap_paper_from_page(oap_printer_prefs_paper(with_paper(OAP_PT_LETTER), sizeof openprinter)) == OAP_PAPER_LETTER &&
          oap_paper_from_page(oap_printer_prefs_paper(with_paper(OAP_PT_A5), sizeof openprinter)) == OAP_PAPER_A5,
          "the lab's two: Letter, then A5");

    /* not a whole FORM PREF: none */
    CHECK(oap_printer_prefs_paper(NULL, 0) == OAP_PAGE_NONE, "no bytes");
    CHECK(oap_printer_prefs_paper(openprinter, 11) == OAP_PAGE_NONE, "under 12 bytes");
    CHECK(oap_printer_prefs_paper(openprinter, sizeof openprinter - 1) == OAP_PAGE_NONE, "cut short");
    memcpy(b, openprinter, sizeof openprinter);
    b[8] = 'X';
    CHECK(oap_printer_prefs_paper(b, sizeof openprinter) == OAP_PAGE_NONE, "FORM but not PREF");
    memcpy(b, openprinter, sizeof openprinter);
    b[0] = 'L';
    CHECK(oap_printer_prefs_paper(b, sizeof openprinter) == OAP_PAGE_NONE, "not a FORM");
    memcpy(b, openprinter, sizeof openprinter);
    memcpy(b + 12, "FORM", 4);
    CHECK(oap_printer_prefs_paper(b, sizeof openprinter) == OAP_PAGE_NONE, "a FORM inside");
    memcpy(b, openprinter, sizeof openprinter);
    b[12] = 1;
    CHECK(oap_printer_prefs_paper(b, sizeof openprinter) == OAP_PAGE_NONE, "a chunk ID that isn't one");
    memcpy(b, openprinter, sizeof openprinter);
    put32(b + 30, 0x1000);
    CHECK(oap_printer_prefs_paper(b, sizeof openprinter) == OAP_PAGE_NONE, "PTXT longer than the file");

    /* read as the page reads it: with more bytes after the FORM, the FORM's own */
    memcpy(b, with_paper(OAP_PT_LEGAL), sizeof openprinter);
    memset(b + sizeof openprinter, 0xff, 64);
    CHECK(oap_printer_prefs_paper(b, sizeof b) == OAP_PT_LEGAL, "bytes after the FORM: the FORM's paper");

    /* a short PTXT reads as zeros past its end; none at all is printer.device's no-file state: both 0 (US Letter) */
    {
        static const unsigned char shortp[] = { 'F','O','R','M',0,0,0,22, 'P','R','E','F', 'P','T','X','T',0,0,0,10,
                                                1,2,3,4,5,6,7,8,9,10 };
        static const unsigned char nopt[] = { 'F','O','R','M',0,0,0,18, 'P','R','E','F', 'P','R','H','D',0,0,0,6,
                                              0,0,0,0,0,0 };
        static const unsigned char empty[] = { 'F','O','R','M',0,0,0,4, 'P','R','E','F' };
        unsigned char odd[12 + 8 + 51];
        memset(odd, 0, sizeof odd);
        memcpy(odd, "FORM", 4); put32(odd + 4, sizeof odd - 8); memcpy(odd + 8, "PREF", 4);
        memcpy(odd + 12, "PTXT", 4); put32(odd + 16, 51);
        odd[20 + 50] = 0x0a;                    /* the size's high byte only: 0x0a00, as the page reads it */
        CHECK(oap_printer_prefs_paper(shortp, sizeof shortp) == 0, "a short PTXT: 0");
        CHECK(oap_printer_prefs_paper(nopt, sizeof nopt) == 0, "no PTXT: 0");
        CHECK(oap_printer_prefs_paper(empty, sizeof empty) == 0, "an empty FORM PREF: 0");
        CHECK(oap_printer_prefs_paper(odd, sizeof odd) == 0x0a00, "a PTXT ending in the size: its high byte");
        CHECK(oap_paper_from_page(0x0a00) == OAP_PAPER_A4, "and that's A4");
    }
}

static void test_job(void)
{
    CHECK(oap_job_paper("0\n") == OAP_PAPER_A4 && oap_job_paper("1\n") == OAP_PAPER_LETTER, "paper=0 A4, 1 Letter");
    CHECK(oap_job_paper("2") == OAP_PAPER_LEGAL && oap_job_paper("3") == OAP_PAPER_A3 && oap_job_paper("4\r\n") == OAP_PAPER_A5,
          "paper=2 Legal, 3 A3, 4 A5");
    CHECK(oap_job_paper("5") == -1 && oap_job_paper("-1") == -1 && oap_job_paper("") == -1 && oap_job_paper("\n") == -1 &&
          oap_job_paper("A4") == -1 && oap_job_paper(NULL) == -1, "anything else: none");
}

/* where the window starts, and what's left remembered */
static int start(int job, int page, int kept, int kept_page, int want_kept)
{
    int k = kept, kp = kept_page, p = oap_paper_start(job, page, &k, &kp);
    if (k != want_kept || (k >= 0 && kp != kept_page) || (page >= 0 && kp != page))
        return -100 - k;
    return p;
}

static void test_start(void)
{
    const int none = OAP_PAGE_NONE, unknown = OAP_PAGE_UNKNOWN;
    /* the .job first */
    CHECK(start(OAP_PAPER_A4, OAP_PT_LETTER, OAP_PAPER_A5, OAP_PT_LETTER, OAP_PAPER_A5) == OAP_PAPER_A4, "the .job's A4 wins");
    CHECK(start(OAP_PAPER_LETTER, none, -1, unknown, -1) == OAP_PAPER_LETTER, "the .job's Letter, nothing else");
    CHECK(start(OAP_PAPER_A4, OAP_PT_A5, OAP_PAPER_LETTER, OAP_PT_A4, -1) == OAP_PAPER_A4, "the .job's, a stale choice dropped");
    /* then the window's own choice, while the page is still on the paper it was chosen against */
    CHECK(start(-1, OAP_PT_A5, OAP_PAPER_LETTER, OAP_PT_A5, OAP_PAPER_LETTER) == OAP_PAPER_LETTER, "chosen since the page's A5");
    CHECK(start(-1, OAP_PT_A5, OAP_PAPER_LETTER, OAP_PT_A4, -1) == OAP_PAPER_A5, "the page changed since: the page's");
    CHECK(start(-1, OAP_PT_A5, OAP_PAPER_LETTER, unknown, -1) == OAP_PAPER_A5, "an old remembered paper: the page's");
    CHECK(start(-1, OAP_PT_LETTER, OAP_PAPER_A3, none, -1) == OAP_PAPER_LETTER, "chosen with no page then: the page's");
    /* then the page */
    CHECK(start(-1, OAP_PT_LETTER, -1, unknown, -1) == OAP_PAPER_LETTER, "the page's Letter");
    CHECK(start(-1, OAP_PT_LEGAL, -1, OAP_PT_LEGAL, -1) == OAP_PAPER_LEGAL, "the page's Legal");
    CHECK(start(-1, 5, -1, unknown, -1) == OAP_PAPER_A4, "the page's A0: A4, as the driver prints it");
    /* no printer.prefs: the window's own, else A4 */
    CHECK(start(-1, none, OAP_PAPER_A3, none, OAP_PAPER_A3) == OAP_PAPER_A3, "no page: the window's A3");
    CHECK(start(-1, none, OAP_PAPER_A3, unknown, OAP_PAPER_A3) == OAP_PAPER_A3, "no page: an old remembered A3");
    CHECK(start(-1, none, OAP_PAPER_A3, OAP_PT_A5, OAP_PAPER_A3) == OAP_PAPER_A3, "printer.prefs gone: the window's A3 kept");
    CHECK(start(-1, none, -1, unknown, -1) == OAP_PAPER_A4, "nothing at all: A4");
    CHECK(start(7, none, -1, unknown, -1) == OAP_PAPER_A4, "an unknown .job number: A4");
}

static void test_settings(void)
{
    OAPJobOptions o;
    char text[96];
    int kept = 99, page = 99;

    /* written before 25 October 2026: five numbers, no page */
    oap_job_defaults(&o);
    CHECK(oap_settings_read("2 1 1 0 2", &o, &kept, &page), "five numbers");
    CHECK(o.copies == 2 && o.orientation == OAP_LANDSCAPE && o.color == OAP_MONO && o.duplex == OAP_DUPLEX_SHORT,
          "copies, layout, colour, sides");
    CHECK(o.paper == OAP_PAPER_A4, "the paper isn't put in the options (where to start is oap_paper_start's)");
    CHECK(kept == OAP_PAPER_LETTER && page == OAP_PAGE_UNKNOWN, "Letter remembered, the page not known (%d %d)", kept, page);

    oap_job_defaults(&o);
    oap_settings_write(text, sizeof text, &o, OAP_PAPER_A5, OAP_PT_LETTER);
    CHECK(!strcmp(text, "1 4 0 1 0 0"), "written: %s", text);
    CHECK(oap_settings_read(text, &o, &kept, &page) && kept == OAP_PAPER_A5 && page == OAP_PT_LETTER, "read back");
    oap_settings_write(text, sizeof text, &o, -1, OAP_PAGE_NONE);
    CHECK(!strcmp(text, "1 -1 0 1 0 -1"), "none remembered: %s", text);
    CHECK(oap_settings_read(text, &o, &kept, &page) && kept == -1 && page == OAP_PAGE_NONE, "read back: none, no page");
    oap_settings_write(text, sizeof text, &o, OAP_PAPER_A3, OAP_PAGE_UNKNOWN);
    CHECK(oap_settings_read(text, &o, &kept, &page) && kept == OAP_PAPER_A3 && page == OAP_PAGE_UNKNOWN, "unknown kept");

    CHECK(oap_settings_read("1 9 0 1 0 9", &o, &kept, &page) && kept == -1 && page == OAP_PT_A4, "an unknown paper: none");
    CHECK(oap_settings_read("1 0 0 1 0 70000", &o, &kept, &page) && page == OAP_PAGE_UNKNOWN, "a page out of range: unknown");
    CHECK(oap_settings_read("0 0 0 1 0 9", &o, &kept, &page) && o.copies == 1, "copies out of range: as they were");
    kept = page = 99;
    CHECK(!oap_settings_read("1 0 0 1", &o, &kept, &page) && !oap_settings_read("", &o, &kept, &page) &&
          !oap_settings_read(NULL, &o, &kept, &page) && kept == 99 && page == 99, "not settings: nothing changed");
}

/* One OpenPrint run as the Print window does it: the remembered settings and the page's printer.prefs in, the
 * paper it starts on out; Print, with the paper chosen in the window or not, writes the settings back. */
static char var[96];
static int run(const unsigned char *prefs, int job, int choose)
{
    OAPJobOptions o;
    int kept = -1, kept_page = OAP_PAGE_UNKNOWN, page, first;
    oap_job_defaults(&o);
    if (var[0]) oap_settings_read(var, &o, &kept, &kept_page);
    page = prefs ? oap_printer_prefs_paper(prefs, sizeof openprinter) : OAP_PAGE_NONE;
    first = o.paper = oap_paper_start(job, page, &kept, &kept_page);
    if (choose >= 0) {                           /* chosen in the window, then Print */
        o.paper = choose;
        kept = choose;
        kept_page = page;
    }
    oap_settings_write(var, sizeof var, &o, kept, kept_page);
    return first;
}

/* The lab's steps, on the host */
static void test_story(void)
{
    var[0] = 0;
    CHECK(run(NULL, -1, -1) == OAP_PAPER_A4, "a new disk: A4");
    CHECK(run(with_paper(OAP_PT_A4), -1, OAP_PAPER_LEGAL) == OAP_PAPER_A4, "OpenPrinter's A4; Legal chosen");
    CHECK(run(with_paper(OAP_PT_A4), -1, -1) == OAP_PAPER_LEGAL, "the page unchanged: Legal, as chosen");
    CHECK(run(with_paper(OAP_PT_LETTER), -1, -1) == OAP_PAPER_LETTER, "Letter on the page: Letter");
    CHECK(run(with_paper(OAP_PT_LETTER), -1, -1) == OAP_PAPER_LETTER, "and again (Print didn't remember the start)");
    CHECK(run(with_paper(OAP_PT_A5), -1, -1) == OAP_PAPER_A5, "A5 on the page: A5");
    CHECK(run(with_paper(OAP_PT_A5), OAP_PAPER_A4, -1) == OAP_PAPER_A4, "a .job saying A4: A4");
    CHECK(run(with_paper(OAP_PT_A5), -1, -1) == OAP_PAPER_A5, "after it, without a .job: A5 again");
    CHECK(run(with_paper(OAP_PT_A5), -1, OAP_PAPER_A3) == OAP_PAPER_A5, "A3 chosen in the window");
    CHECK(run(with_paper(OAP_PT_A5), -1, -1) == OAP_PAPER_A3, "the window's A3, the page still on A5");
    CHECK(run(with_paper(OAP_PT_LETTER), -1, -1) == OAP_PAPER_LETTER, "Letter on the page again: Letter");
    CHECK(run(with_paper(OAP_PT_A5), -1, -1) == OAP_PAPER_A5, "A5 on the page: A5, not the A3 chosen against A5 before");
    /* the one it can't see: the page changed and back with no Print between (the window's choice comes back) */
    CHECK(run(with_paper(OAP_PT_A5), -1, OAP_PAPER_A3) == OAP_PAPER_A5 && run(with_paper(OAP_PT_A5), -1, -1) == OAP_PAPER_A3,
          "A3 chosen against A5 again");
    CHECK(run(with_paper(OAP_PT_A5), -1, -1) == OAP_PAPER_A3, "Letter and back to A5 on the page, no Print between: A3");
    /* an OpenPrint from before 25 October left Letter */
    strcpy(var, "1 1 0 1 0");
    CHECK(run(with_paper(OAP_PT_A5), -1, -1) == OAP_PAPER_A5, "an older OpenPrint's Letter: the page's A5");
    strcpy(var, "1 1 0 1 0");
    CHECK(run(NULL, -1, -1) == OAP_PAPER_LETTER, "with no printer.prefs: its Letter, as before");
}

int main(void)
{
    test_page_mapping();
    test_sizes();
    test_ipp();
    test_prefs_file();
    test_job();
    test_start();
    test_settings();
    test_story();
    if (fails) {
        printf("FAIL: %d of %d paper checks\n", fails, checks);
        return 1;
    }
    printf("PASS: %d paper checks (sizes, IPP media, printer.prefs, .job, where the Print window starts)\n", checks);
    return 0;
}
