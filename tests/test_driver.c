/* Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT */
/* Host test of OpenPrint's printer driver (src/driver/openprint_driver.c), the PDF writer behind
 * printer.device: it is driven as printer.device drives it (Init, Open, ConvFunc for characters, DoSpecial for
 * a new line, Render for a picture, Close), its bytes are caught, and the PDF is checked: every xref entry points
 * at its object, startxref at the xref, the page count, each page's MediaBox, the font, the lines on each page,
 * and that every AllocMem was freed. Stub headers: tests/host/driver (names only, not the NDK). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/execbase.h>
#include <devices/printer.h>
#include <devices/prtbase.h>
#include <devices/prtgfx.h>

/* the driver's entry points and globals */
LONG oap_init(struct PrinterData *pd);
int oap_open(struct IORequest *ior);
void oap_close(struct IORequest *ior);
void oap_expunge(void);
LONG oap_conv(STRPTR buf, TEXT c, LONG crlf);
LONG oap_special(UWORD *command, UBYTE out[], BYTE *pos, BYTE *spacing, BYTE *crlf, STRPTR params);
LONG oap_render(LONG ct, LONG x, LONG y, LONG status, ...);
struct PrinterExtendedData oap_ped;     /* printertag.s's, on the Amiga */

static long allocs;
APTR AllocMem(ULONG size, ULONG flags)
{
    (void)flags;
    allocs++;
    return calloc(1, size);
}
void FreeMem(APTR mem, ULONG size)
{
    (void)size;
    if (mem) {
        allocs--;
        free(mem);
    }
}
void CopyMem(const void *from, APTR to, ULONG size) { memmove(to, from, size); }

static char *out;
static size_t out_len, out_cap;
static long writes;
static LONG pwrite(APTR data, LONG len)
{
    writes++;
    if (out_len + (size_t)len + 1 > out_cap) {
        out_cap = (out_len + (size_t)len + 1) * 2;
        out = realloc(out, out_cap);
    }
    memcpy(out + out_len, data, (size_t)len);
    out_len += (size_t)len;
    out[out_len] = 0;
    return 0;
}
static LONG bothready(void) { return 0; }

static struct PrinterData pd;
static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s: ", name); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static void start(UWORD paper, UWORD length, UWORD pitch, UWORD spacing, UWORD aspect)
{
    memset(&pd, 0, sizeof pd);
    pd.pd_PWrite = pwrite;
    pd.pd_PBothReady = bothready;
    pd.pd_Preferences.PaperSize = paper;
    pd.pd_Preferences.PaperLength = length;
    pd.pd_Preferences.PrintPitch = pitch;
    pd.pd_Preferences.PrintSpacing = spacing;
    pd.pd_Preferences.PrintAspect = aspect;
    out_len = 0;
    writes = 0;
    oap_init(&pd);
    oap_open(NULL);
}

/* text as printer.device hands it over: LF is aNEL to DoSpecial (crlf on), everything else through ConvFunc */
static void text(const char *s)
{
    UBYTE o[8];
    BYTE pos = 0, sp = 0, crlf = 1;
    for (; *s; s++) {
        if (*s == '\n') {
            UWORD cmd = aNEL;
            oap_special(&cmd, o, &pos, &sp, &crlf, (STRPTR) "");
        } else {
            oap_conv((STRPTR)o, *s, 1);
        }
    }
}

static const char *find(const char *from, const char *what) { return strstr(from, what); }
static int count(const char *what)
{
    int n = 0;
    const char *p = out;
    while ((p = strstr(p, what)) != NULL) {
        n++;
        p += strlen(what);
    }
    return n;
}

/* the PDF's own structure: %%EOF at the end, startxref at "xref", each in-use entry at "N 0 obj" */
static void check_pdf(const char *name)
{
    const char *sx, *xr, *e;
    long xref, n, i;
    CHECK(out_len > 9 && !memcmp(out, "%PDF-1.4\n", 9), "no PDF header");
    CHECK(out_len > 6 && !strcmp(out + out_len - 6, "%%EOF\n"), "no %%%%EOF at the end");
    sx = find(out, "startxref\n");
    CHECK(sx != NULL, "no startxref");
    if (!sx)
        return;
    xref = strtol(sx + 10, NULL, 10);
    CHECK(xref > 0 && (size_t)xref < out_len && !strncmp(out + xref, "xref\n0 ", 7), "startxref %ld is not the xref", xref);
    if (strncmp(out + xref, "xref\n0 ", 7))
        return;
    n = strtol(out + xref + 7, (char **)&e, 10);
    e++;                                            /* past the \n */
    for (i = 1; i < n; i++) {
        const char *ent = e + 20 * i;
        long off = strtol(ent, NULL, 10);
        char want[32];
        snprintf(want, sizeof want, "%ld 0 obj\n", i);
        CHECK(off >= 0 && (size_t)off < out_len && !strncmp(out + off, want, strlen(want)), "xref entry %ld points at %ld, not its object", i, off);
    }
    xr = find(out, "/Type /Pages /Count ");
    CHECK(xr != NULL, "no Pages");
}

static long pages(void)
{
    const char *p = find(out, "/Type /Pages /Count ");
    return p ? strtol(p + 20, NULL, 10) : -1;
}

/* the lines on each page: Tj operators between BT and ET, counted per content stream */
static int max_lines_on_a_page(void)
{
    int most = 0;
    const char *p = out;
    while ((p = strstr(p, "stream\n")) != NULL) {
        const char *end = strstr(p, "endstream");
        int n = 0;
        const char *q = p;
        if (!end)
            break;
        while ((q = strstr(q, ") Tj")) != NULL && q < end) {
            n++;
            q += 4;
        }
        if (n > most)
            most = n;
        p = end + 9;
    }
    return most;
}

int main(void)
{
    char line[64], *big;
    const char *name;
    int i;

    name = "300 lines, A4, 66 to a page";
    start(EURO_A4, 66, PICA, SIX_LPI, ASPECT_HORIZ);
    big = malloc(300 * 64 + 1);
    big[0] = 0;
    for (i = 1; i <= 300; i++) {
        snprintf(line, sizeof line, "%04d The quick brown fox jumps over the lazy dog.\n", i);
        strcat(big, line);
    }
    text(big);
    oap_close(NULL);
    check_pdf(name);
    CHECK(pages() == 5, "%ld pages, not 5", pages());
    CHECK(count("/MediaBox [0 0 595 842]") == 5, "pages not A4 upright: %d", count("/MediaBox [0 0 595 842]"));
    CHECK(count("/BaseFont /Courier") == 1 && !count("Helvetica"), "not Courier");
    CHECK(count("/F1 10 Tf") >= 5, "not 10 point at pica");
    CHECK(max_lines_on_a_page() == 66, "%d lines on the fullest page, not 66", max_lines_on_a_page());
    CHECK(find(out, "(0300 The quick") != NULL, "line 300 missing");
    CHECK(find(out, "0 -11.7 Td") != NULL, "66 lines on A4: spacing 11.7 expected");
    CHECK(allocs == 0, "%ld AllocMem not freed", allocs);
    CHECK(writes > 300 && writes < 340, "%ld writes to the port for 300 lines: a line at a time expected", writes);

    name = "2700 lines: 41 pages (more than 32)";
    start(EURO_A4, 66, PICA, SIX_LPI, ASPECT_HORIZ);
    for (i = 0; i < 9; i++)
        text(big);
    oap_close(NULL);
    check_pdf(name);
    CHECK(pages() == 41, "%ld pages, not 41", pages());
    CHECK(allocs == 0, "%ld AllocMem not freed", allocs);

    name = "form feeds: 40 pages";
    start(EURO_A4, 66, PICA, SIX_LPI, ASPECT_HORIZ);
    for (i = 1; i <= 40; i++) {
        snprintf(line, sizeof line, "page %d\n\f", i);
        text(line);
    }
    oap_close(NULL);
    check_pdf(name);
    CHECK(pages() == 40, "%ld pages, not 40", pages());
    CHECK(find(out, "(page 40) Tj") != NULL, "page 40's text missing");

    name = "US Letter, elite, 8 lines to the inch";
    start(US_LETTER, 66, ELITE, EIGHT_LPI, ASPECT_HORIZ);
    text("one\ntwo\n");
    oap_close(NULL);
    check_pdf(name);
    CHECK(count("/MediaBox [0 0 612 792]") == 1, "not Letter upright");
    CHECK(count("/F1 8 Tf") == 1, "not 8 point at elite");
    CHECK(find(out, "0 -9 Td") != NULL, "8 lines to the inch: 9 points expected");

    name = "US Legal, fine, paper length 0 (66)";
    start(US_LEGAL, 0, FINE, SIX_LPI, ASPECT_VERT);
    text("legal\n");
    oap_close(NULL);
    check_pdf(name);
    CHECK(count("/MediaBox [0 0 612 1008]") == 1, "not Legal upright");
    CHECK(count("/F1 6 Tf") == 1, "not 6 point at fine");
    CHECK(find(out, "0 -12 Td") != NULL, "6 lines to the inch: 12 points expected");

    name = "a picture, aspect horizontal: a turned page";
    start(EURO_A4, 66, PICA, SIX_LPI, ASPECT_HORIZ);
    {
        union colorEntry ci[4];
        UWORD sx[4] = { 1, 1, 1, 1 };
        struct PrtInfo pi;
        struct IODRPReq rq;
        memset(ci, 0, sizeof ci);
        ci[0].colorByte[PCMRED] = 15;
        pi.pi_ColorInt = ci;
        pi.pi_ScaleX = sx;
        pi.pi_width = 4;
        pi.pi_xpos = 1;
        CHECK(oap_render((LONG)(long)&rq, SPECIAL_DENSITY2, 0, 5) == 0, "PREINIT failed");
        CHECK(oap_ped.ped_XDotsInch == 100 && oap_ped.ped_MaxXDots == 842 * 100 / 72, "dots %u, %lu", oap_ped.ped_XDotsInch, (unsigned long)oap_ped.ped_MaxXDots);
        CHECK(oap_render((LONG)(long)&rq, 5, 2, 0) == 0, "INIT failed");
        for (i = 0; i < 2; i++) {
            oap_render(0, 0, 0, 3);
            oap_render((LONG)(long)&pi, 0, i, 1);
            oap_render(0, 0, 1, 2);
        }
        oap_render(0, 0, 0, 4);
    }
    text("after\n");
    oap_close(NULL);
    check_pdf(name);
    CHECK(pages() == 2, "%ld pages, not 2 (the picture's, then the text's)", pages());
    CHECK(count("/MediaBox [0 0 842 595]") == 1 && count("/MediaBox [0 0 595 842]") == 1, "the picture's page turned, the text's upright");
    CHECK(find(out, "BI /W 5 /H 2") != NULL, "the picture's size");
    CHECK(find(out, "FFFFFFFF0000FFFFFF") != NULL || find(out, "FFFFFFFF0000") != NULL, "the red pixel at xpos 1");
    CHECK(allocs == 0, "%ld AllocMem not freed", allocs);

    oap_expunge();
    free(big);
    free(out);
    if (failures) {
        printf("test_driver: %d failed\n", failures);
        return 1;
    }
    printf("test_driver: all passed\n");
    return 0;
}
