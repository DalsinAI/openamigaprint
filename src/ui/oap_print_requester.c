/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* OpenAmigaPrint's Print requester, in GadTools (Dale, 4 October 2026: OS
 * 3.x applications use GadTools or MUI, not ReAction). It keeps the
 * contract of src/amiga/ui.c, oap_run_print_dialog(), and the 3 October
 * review's design:
 *   - the printer is chosen by name from one list that also offers
 *     "Save as PDF file"; only printers verified to take PDF are offered;
 *   - copies, pages, paper, layout, sides and colour are labelled gadgets;
 *   - the preview is drawn to the paper's real shape;
 *   - progress shows in a status line and a bar;
 *   - the window follows the screen's font (Topaz 8 if it would not fit),
 *     opens centred, every control has a key, and the last settings are
 *     remembered.
 * Sending stays with C:OAVWorker, and its rules stay: an upload whose outcome
 * is uncertain keeps Print disabled so a job is never sent twice. */
#include "oap.h"
#include "oap_discovery.h"
#include "oap_gt.h"
#include "oap_printers.h"
#include "oap_selection.h"
#include "oav_jobs.h"

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <graphics/gfxbase.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *GadToolsBase, *AslBase;

#define SETTINGS_VAR "OpenAmigaPrint/PrintSettings"

enum {
    G_PRINTER = 1, G_FIND, G_INFO, G_COPIES, G_LESS, G_MORE, G_PAGES, G_RANGE, G_PAPER, G_LAYOUT, G_SIDES, G_COLOUR,
    G_STATUS, G_PRINT, G_SAVE, G_CANCEL
};

static STRPTR paper_labels[] = { (STRPTR)"A4 (210 \xd7 297 mm)", (STRPTR)"Letter (8.5 \xd7 11 in)", NULL };
static STRPTR layout_labels[] = { (STRPTR)"Portrait", (STRPTR)"Landscape", NULL };
static STRPTR sides_labels[] = { (STRPTR)"One-sided", (STRPTR)"Two-sided, long edge", (STRPTR)"Two-sided, short edge", NULL };
static STRPTR colour_labels[] = { (STRPTR)"Colour", (STRPTR)"Black and white", NULL };
static STRPTR pages_labels[] = { (STRPTR)"All", (STRPTR)"Range", NULL };

/* Where everything goes, worked out from the font. */
typedef struct Geo {
    int label_w, ctl_x, ctl_w, find_w;
    int px, py, pw, ph;                  /* the preview group */
    int rx, rw;                          /* the right-hand column */
    int y_printer, y_info;
    int g1y, g1h, y_copies, y_pages, mx_pitch;
    int g2y, g2h, y_paper;
    int y_status, prog_w, y_buttons;
    int inner_w, inner_h;
} Geo;

static struct PrintRequester {
    OAPGT g;
    Geo geo;
    struct Window *win;
    struct Gadget *glist;
    OAPPrinterList printers;
    int offered[OAP_PRINTERS_MAX];             /* cycle row -> printer index */
    int offered_count;
    STRPTR printer_labels[OAP_PRINTERS_MAX + 1];
    const char *pdf;
    long pdf_bytes;
    OAPJobOptions *options;
    char status_text[256], info_text[200], range_text[16];
    char request[OAV_PATH_MAX];
    int row, copies, pages, sides;             /* what the gadgets show */
    int busy, sent, percent, finished, ticks;
} R;

static void copy(char *dst, size_t cap, const char *src)
{
    if (!cap)
        return;
    strncpy(dst, src ? src : "", cap - 1);
    dst[cap - 1] = 0;
}

static long file_size(const char *path)
{
    BPTR lock = Lock((STRPTR)path, ACCESS_READ);
    struct FileInfoBlock *fib;
    long n = -1;
    if (!lock)
        return -1;
    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib && Examine(lock, fib))
        n = fib->fib_Size;
    if (fib)
        FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    return n;
}

static const char *base_name(const char *path)
{
    const char *p = FilePart((STRPTR)path);
    return p && *p ? p : path;
}

/* A queued PDF's .job names what was printed ("title="); else the file's name. */
static const char *job_title(const char *pdf)
{
    static char title[120];
    char path[300], line[300];
    size_t n = strlen(pdf);
    FILE *f;
    if (n > 4 && n < sizeof(path) && !strcmp(pdf + n - 4, ".pdf")) {
        strcpy(path, pdf);
        strcpy(path + n - 4, ".job");
        if ((f = fopen(path, "r"))) {
            while (fgets(line, sizeof(line), f))
                if (!strncmp(line, "title=", 6) && line[6] && line[6] != '\n') {
                    line[strcspn(line, "\r\n")] = 0;
                    copy(title, sizeof(title), line + 6);
                    fclose(f);
                    return title;
                }
            fclose(f);
        }
    }
    return base_name(pdf);
}

/* A page OpenAmigaView laid out says its paper and orientation in its .job
 * (paper=0 A4 / 1 Letter, landscape=0/1): the Print requester starts there. */
static void job_page_setup(const char *pdf, OAPJobOptions *o)
{
    char path[300], line[120];
    size_t n = strlen(pdf);
    FILE *f;
    if (n <= 4 || n >= sizeof(path) || strcmp(pdf + n - 4, ".pdf"))
        return;
    strcpy(path, pdf);
    strcpy(path + n - 4, ".job");
    if (!(f = fopen(path, "r")))
        return;
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "paper=", 6))
            o->paper = atoi(line + 6) ? OAP_PAPER_LETTER : OAP_PAPER_A4;
        else if (!strncmp(line, "landscape=", 10))
            o->orientation = atoi(line + 10) ? OAP_LANDSCAPE : OAP_PORTRAIT;
    }
    fclose(f);
}

/* ---- remembered settings ---------------------------------------------- */

static void load_settings(OAPJobOptions *o)
{
    char text[96];
    int copies, paper, layout, colour, sides;
    if (GetVar((STRPTR)SETTINGS_VAR, (STRPTR)text, sizeof(text), GVF_GLOBAL_ONLY) <= 0)
        return;
    if (sscanf(text, "%d %d %d %d %d", &copies, &paper, &layout, &colour, &sides) == 5) {
        if (copies >= 1 && copies <= 99)
            o->copies = copies;
        o->paper = paper == OAP_PAPER_LETTER ? OAP_PAPER_LETTER : OAP_PAPER_A4;
        o->orientation = layout == OAP_LANDSCAPE ? OAP_LANDSCAPE : OAP_PORTRAIT;
        o->color = colour ? OAP_COLOR : OAP_MONO;
        o->duplex = sides >= OAP_SIMPLEX && sides <= OAP_DUPLEX_SHORT ? sides : OAP_SIMPLEX;
    }
}

static void save_settings(const OAPJobOptions *o)
{
    char text[96];
    snprintf(text, sizeof(text), "%d %d %d %d %d", o->copies, o->paper, o->orientation, o->color, o->duplex);
    SetVar((STRPTR)SETTINGS_VAR, (STRPTR)text, (LONG)strlen(text), GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
}

/* ---- the printers ------------------------------------------------------ */

static const OAPPrinter *chosen(void)
{
    if (R.row < 0 || R.row >= R.offered_count)
        return NULL;
    return &R.printers.printer[R.offered[R.row]];
}

/* Save as PDF file and every printer verified to take PDF; `uri` is
 * selected when it is offered. */
static void fill_printers(const char *uri)
{
    int i;
    oap_printers_load(&R.printers);
    R.offered_count = 0;
    R.row = 0;
    for (i = 0; i < R.printers.count && R.offered_count < OAP_PRINTERS_MAX; i++) {
        const OAPPrinter *p = &R.printers.printer[i];
        if (!oap_printer_is_file(p) && p->pdf != OAP_PDF_YES)
            continue;
        if (uri && !strcmp(p->uri, uri))
            R.row = R.offered_count;
        R.offered[R.offered_count] = i;
        R.printer_labels[R.offered_count++] = (STRPTR)p->name;
    }
    R.printer_labels[R.offered_count] = NULL;
}

static void printer_info(void)
{
    const OAPPrinter *p = chosen();
    if (!p)
        copy(R.info_text, sizeof(R.info_text), "No printer chosen");
    else if (oap_printer_is_file(p))
        copy(R.info_text, sizeof(R.info_text), "Print saves the document as a PDF file on this Amiga");
    else
        copy(R.info_text, sizeof(R.info_text), p->note[0] ? p->note : "Verified to print PDF");
}

/* ---- layout -------------------------------------------------------------- */

static int max_label(STRPTR *labels)
{
    int w = 0;
    for (; *labels; labels++) {
        int t = oap_gt_text_w(&R.g, (char *)*labels);
        if (t > w)
            w = t;
    }
    return w;
}

static void layout(void)
{
    static const char *labels[] = { "P_rinter", "C_opies", "Pages", "Si_ze", "_Layout", "S_ides", "Colo_ur" };
    Geo *G = &R.geo;
    OAPGT *g = &R.g;
    int i, cyc, w, gh = g->gad_h, mxh;
    memset(G, 0, sizeof(*G));
    for (i = 0; i < (int)(sizeof(labels) / sizeof(labels[0])); i++)
        if ((w = oap_gt_text_w(g, labels[i])) > G->label_w)
            G->label_w = w;
    cyc = max_label(paper_labels);
    if ((w = max_label(sides_labels)) > cyc) cyc = w;
    if ((w = max_label(R.printer_labels)) > cyc) cyc = w;
    cyc += 36;                                       /* the cycle glyph and its edges */
    if (cyc < 160) cyc = 160;
    G->find_w = oap_gt_text_w(g, "_Find printers...") + 16;
    G->rw = G->label_w + OAP_GT_GAP + cyc + OAP_GT_GAP + G->find_w;
    if ((w = 2 * OAP_GT_INSET + G->label_w + OAP_GT_GAP + cyc) > G->rw)
        G->rw = w;
    G->pw = 150 * g->fh / 8;
    mxh = g->fh > 9 ? g->fh : 9;
    G->mx_pitch = gh + 2;

    G->px = OAP_GT_MARGIN;
    G->py = OAP_GT_MARGIN;
    G->rx = G->px + G->pw + 2 * OAP_GT_GAP;
    G->y_printer = G->py + g->fh / 2;
    G->y_info = G->y_printer + gh + 3;
    G->g1y = G->y_info + g->line_h + OAP_GT_GAP;
    G->y_copies = G->g1y + g->fh + OAP_GT_GAP;
    G->y_pages = G->y_copies + gh + OAP_GT_GAP + (gh - mxh) / 2;
    G->g1h = (G->y_pages - (gh - mxh) / 2 + 2 * G->mx_pitch + OAP_GT_GAP) - G->g1y;
    G->g2y = G->g1y + G->g1h + OAP_GT_GAP;
    G->y_paper = G->g2y + g->fh + OAP_GT_GAP;
    G->g2h = g->fh + OAP_GT_GAP + 4 * (gh + 4) + OAP_GT_GAP - 4;
    G->ph = G->g2y + G->g2h - G->py;
    G->ctl_x = G->rx + G->label_w + OAP_GT_GAP;
    G->ctl_w = G->rx + G->rw - G->ctl_x;
    G->y_status = G->py + G->ph + OAP_GT_GAP;
    G->prog_w = oap_gt_text_w(g, "100%") + 70;
    G->y_buttons = G->y_status + gh + OAP_GT_GAP;
    G->inner_w = G->rx + G->rw + OAP_GT_MARGIN;
    G->inner_h = G->y_buttons + gh + OAP_GT_MARGIN;
}

/* ---- the gadgets ------------------------------------------------------- */

static struct Gadget *gad(UWORD id)
{
    return oap_gt_find(R.glist, id);
}

/* What the string and number gadgets hold, before they are rebuilt. */
static void keep_typing(void)
{
    struct Gadget *c = gad(G_COPIES), *r = gad(G_RANGE);
    if (c) {
        LONG n = ((struct StringInfo *)c->SpecialInfo)->LongInt;
        R.copies = n < 1 ? 1 : n > 99 ? 99 : (int)n;
    }
    if (r)
        copy(R.range_text, sizeof(R.range_text), (char *)((struct StringInfo *)r->SpecialInfo)->Buffer);
}

static void build(void)
{
    OAPGT *g = &R.g;
    Geo *G = &R.geo;
    struct Gadget *p;
    int bx = R.win->BorderLeft, by = R.win->BorderTop, gh = g->gad_h, bw, i;
    const OAPPrinter *pr = chosen();
    int file = pr && oap_printer_is_file(pr);
    int iw = G->inner_w - 2 * OAP_GT_MARGIN;
    static STRPTR *cycles[] = { paper_labels, layout_labels, sides_labels, colour_labels };
    static const char *cycle_names[] = { "Si_ze", "_Layout", "S_ides", "Colo_ur" };
    int actives[4];

    actives[0] = R.options->paper == OAP_PAPER_LETTER;
    actives[1] = R.options->orientation == OAP_LANDSCAPE;
    actives[2] = R.sides;
    actives[3] = R.options->color ? 0 : 1;
    printer_info();
    R.glist = NULL;
    p = CreateContext(&R.glist);
    p = CreateGadget(CYCLE_KIND, p, oap_gt_ng(g, bx + G->ctl_x, by + G->y_printer, G->rw - (G->ctl_x - G->rx) - OAP_GT_GAP - G->find_w, gh,
                     "P_rinter", G_PRINTER, PLACETEXT_LEFT, 0),
                     GTCY_Labels, (ULONG)R.printer_labels, GTCY_Active, R.row, GT_Underscore, '_', GA_Disabled, R.busy, TAG_DONE);
    p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, bx + G->rx + G->rw - G->find_w, by + G->y_printer, G->find_w, gh,
                     "_Find printers...", G_FIND, PLACETEXT_IN, 0), GT_Underscore, '_', GA_Disabled, R.busy, TAG_DONE);
    p = CreateGadget(TEXT_KIND, p, oap_gt_ng(g, bx + G->ctl_x, by + G->y_info, G->rx + G->rw - G->ctl_x, g->line_h, NULL, G_INFO, 0, 0),
                     GTTX_Text, (ULONG)R.info_text, GTTX_CopyText, TRUE, TAG_DONE);

    /* copies and pages */
    {
        int cx = G->rx + OAP_GT_INSET + G->label_w + OAP_GT_GAP, nw = oap_gt_text_w(g, "000") + 16;
        p = CreateGadget(INTEGER_KIND, p, oap_gt_ng(g, bx + cx, by + G->y_copies, nw, gh, "C_opies", G_COPIES, PLACETEXT_LEFT, 0),
                         GTIN_Number, R.copies, GTIN_MaxChars, 2, GT_Underscore, '_', TAG_DONE);
        p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, bx + cx + nw + 2, by + G->y_copies, gh + 4, gh, "-", G_LESS, PLACETEXT_IN, 0), TAG_DONE);
        p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, bx + cx + nw + gh + 8, by + G->y_copies, gh + 4, gh, "+", G_MORE, PLACETEXT_IN, 0), TAG_DONE);
        p = CreateGadget(MX_KIND, p, oap_gt_ng(g, bx + cx, by + G->y_pages, 17, 9, NULL, G_PAGES, PLACETEXT_RIGHT, 0),
                         GTMX_Labels, (ULONG)pages_labels, GTMX_Active, R.pages, GTMX_Spacing, G->mx_pitch - (g->fh > 9 ? g->fh : 9), TAG_DONE);
        {
            int sx = cx + 17 + 8 + oap_gt_text_w(g, "Range") + OAP_GT_GAP;
            int sy = G->y_pages + G->mx_pitch - (gh - (g->fh > 9 ? g->fh : 9)) / 2 - 1;
            p = CreateGadget(STRING_KIND, p, oap_gt_ng(g, bx + sx, by + sy, G->rx + G->rw - OAP_GT_INSET - sx, gh, NULL, G_RANGE, 0, 0),
                             GTST_String, (ULONG)R.range_text, GTST_MaxChars, 15, GA_Disabled, !R.pages, TAG_DONE);
        }
    }
    /* paper */
    for (i = 0; i < 4; i++)
        p = CreateGadget(CYCLE_KIND, p, oap_gt_ng(g, bx + G->rx + OAP_GT_INSET + G->label_w + OAP_GT_GAP, by + G->y_paper + i * (gh + 4),
                         G->rw - 2 * OAP_GT_INSET - G->label_w - OAP_GT_GAP, gh, cycle_names[i], (UWORD)(G_PAPER + i), PLACETEXT_LEFT, 0),
                         GTCY_Labels, (ULONG)cycles[i], GTCY_Active, actives[i], GT_Underscore, '_', TAG_DONE);
    /* status, then the buttons */
    p = CreateGadget(TEXT_KIND, p, oap_gt_ng(g, bx + OAP_GT_MARGIN, by + G->y_status, iw - G->prog_w - OAP_GT_GAP, gh, NULL, G_STATUS, 0, 0),
                     GTTX_Text, (ULONG)R.status_text, GTTX_Border, TRUE, GTTX_CopyText, TRUE, TAG_DONE);
    bw = (iw - 2 * OAP_GT_GAP) / 3;
    p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, bx + OAP_GT_MARGIN, by + G->y_buttons, bw, gh, file ? "_Save..." : "_Print", G_PRINT, PLACETEXT_IN, 0),
                     GT_Underscore, '_', GA_Disabled, R.busy || R.sent, TAG_DONE);
    p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, bx + OAP_GT_MARGIN + bw + OAP_GT_GAP, by + G->y_buttons, bw, gh, "Save as P_DF...", G_SAVE, PLACETEXT_IN, 0),
                     GT_Underscore, '_', GA_Disabled, R.busy || file, TAG_DONE);
    p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, bx + OAP_GT_MARGIN + 2 * (bw + OAP_GT_GAP), by + G->y_buttons, iw - 2 * (bw + OAP_GT_GAP), gh,
                     R.busy ? "_Stop" : R.finished ? "_Close" : "_Cancel", G_CANCEL, PLACETEXT_IN, 0), GT_Underscore, '_', TAG_DONE);
    (void)p;
}

/* ---- drawing ------------------------------------------------------------- */

static void draw_progress(void)
{
    Geo *G = &R.geo;
    int bx = R.win->BorderLeft, by = R.win->BorderTop;
    oap_gt_progress(&R.g, R.win->RPort, bx + G->inner_w - OAP_GT_MARGIN - G->prog_w, by + G->y_status, G->prog_w, R.g.gad_h,
                    R.percent, R.busy || R.percent > 0);
}

/* The page drawn to the paper's real shape, a sketch of text on it, and
 * the document's size under it. */
static void draw_preview(void)
{
    OAPGT *g = &R.g;
    Geo *G = &R.geo;
    struct RastPort *rp = R.win->RPort;
    int bx = R.win->BorderLeft, by = R.win->BorderTop;
    long pw = R.options->paper == OAP_PAPER_LETTER ? 216 : 210, ph = R.options->paper == OAP_PAPER_LETTER ? 279 : 297;
    long ax = bx + G->px + OAP_GT_INSET, ay = by + G->py + g->fh + OAP_GT_GAP;
    long aw = G->pw - 2 * OAP_GT_INSET, ah = G->ph - g->fh - 2 * OAP_GT_GAP - g->line_h - 4;
    long w, h, x, y, line;
    char caption[64];
    if (R.options->orientation == OAP_LANDSCAPE) {
        long t = pw;
        pw = ph;
        ph = t;
    }
    SetAPen(rp, oap_gt_pen(g, BACKGROUNDPEN));
    RectFill(rp, ax, ay, ax + aw - 1, ay + ah + g->line_h + 3);
    w = aw - 4;
    h = w * ph / pw;
    if (h > ah - 4) {
        h = ah - 4;
        w = h * pw / ph;
    }
    if (w < 8 || h < 8)
        return;
    x = ax + (aw - w) / 2;
    y = ay + (ah - h) / 2;
    SetAPen(rp, oap_gt_pen(g, SHADOWPEN));                     /* the shadow */
    RectFill(rp, x + 3, y + 3, x + w + 2, y + h + 2);
    SetAPen(rp, oap_gt_pen(g, SHINEPEN));                      /* the paper */
    RectFill(rp, x, y, x + w - 1, y + h - 1);
    SetAPen(rp, oap_gt_pen(g, SHADOWPEN));
    Move(rp, x, y);
    Draw(rp, x + w - 1, y);
    Draw(rp, x + w - 1, y + h - 1);
    Draw(rp, x, y + h - 1);
    Draw(rp, x, y);
    SetAPen(rp, oap_gt_pen(g, TEXTPEN));                       /* a heading and lines of text */
    RectFill(rp, x + w / 8, y + h / 10, x + w / 8 + w / 2, y + h / 10 + (h > 120 ? 3 : 1));
    SetAPen(rp, oap_gt_pen(g, FILLPEN));
    for (line = y + h / 10 + h / 12; line < y + h - h / 10; line += h > 160 ? 7 : 5) {
        long end = x + w - w / 8 - ((line / 5) % 3) * (w / 10);
        Move(rp, x + w / 8, line);
        Draw(rp, end, line);
    }
    if (R.pdf_bytes >= 10240)
        snprintf(caption, sizeof(caption), "PDF, %ld KB", R.pdf_bytes / 1024);
    else
        snprintf(caption, sizeof(caption), "PDF, %ld bytes", R.pdf_bytes < 0 ? 0L : R.pdf_bytes);
    w = oap_gt_text_w(g, caption);
    oap_gt_text(g, rp, ax + (aw - w) / 2, ay + ah + 2, caption, TEXTPEN, aw);
}

static void draw_static(void)
{
    OAPGT *g = &R.g;
    Geo *G = &R.geo;
    struct RastPort *rp = R.win->RPort;
    int bx = R.win->BorderLeft, by = R.win->BorderTop;
    oap_gt_group(g, rp, bx + G->px, by + G->py, G->pw, G->ph, "Preview");
    oap_gt_group(g, rp, bx + G->rx, by + G->g1y, G->rw, G->g1h, "Copies and pages");
    oap_gt_text(g, rp, bx + G->rx + OAP_GT_INSET + G->label_w - oap_gt_text_w(g, "Pages"), by + G->y_pages, "Pages", TEXTPEN, 0);
    oap_gt_group(g, rp, bx + G->rx, by + G->g2y, G->rw, G->g2h, "Paper");
    draw_preview();
    draw_progress();
}

/* Builds the gadgets from the state and draws the window again: after a
 * printer or the busy state changes a button's words. */
static void rebuild(void)
{
    if (R.glist) {
        keep_typing();
        RemoveGList(R.win, R.glist, -1);
        FreeGadgets(R.glist);
        R.glist = NULL;
    }
    oap_gt_erase(R.win);
    build();
    if (!R.glist)
        return;
    AddGList(R.win, R.glist, ~0, -1, NULL);
    RefreshGList(R.glist, R.win, NULL, -1);
    GT_RefreshWindow(R.win, NULL);
    draw_static();
}

/* Into the range field, its cursor after what is there. */
static void edit_range(void)
{
    struct Gadget *r = gad(G_RANGE);
    if (!r)
        return;
    ((struct StringInfo *)r->SpecialInfo)->BufferPos = (WORD)strlen((char *)((struct StringInfo *)r->SpecialInfo)->Buffer);
    ActivateGadget(r, R.win, NULL);
}

static void show_status(const char *text)
{
    struct Gadget *s = gad(G_STATUS);
    copy(R.status_text, sizeof(R.status_text), text);
    if (s && R.win)
        GT_SetGadgetAttrs(s, R.win, NULL, GTTX_Text, (ULONG)R.status_text, TAG_DONE);
}

/* ---- actions ----------------------------------------------------------- */

static void read_options(OAPJobOptions *o)
{
    const OAPPrinter *p = chosen();
    int a = 0, b = 0;
    keep_typing();
    copy(o->printer_uri, sizeof(o->printer_uri), p ? p->uri : "");
    o->copies = R.copies;
    o->duplex = R.sides;
    o->page_start = o->page_end = 0;
    if (R.pages) {
        if (sscanf(R.range_text, "%d-%d", &a, &b) == 2 && a > 0 && b >= a) {
            o->page_start = a;
            o->page_end = b;
        } else if (sscanf(R.range_text, "%d", &a) == 1 && a > 0)
            o->page_start = o->page_end = a;
    }
}

static int copy_file(const char *from, const char *to)
{
    FILE *in, *out;
    char *buf;
    size_t n;
    int ok = 1;
    if (!strcmp(from, to))
        return 1;
    in = fopen(from, "rb");
    if (!in)
        return 0;
    buf = malloc(8192);
    out = buf ? fopen(to, "wb") : NULL;
    if (!out) {
        free(buf);
        fclose(in);
        return 0;
    }
    while ((n = fread(buf, 1, 8192, in)) != 0)
        if (fwrite(buf, 1, n, out) != n) {
            ok = 0;
            break;
        }
    if (ferror(in))
        ok = 0;
    fclose(in);
    if (fclose(out))
        ok = 0;
    free(buf);
    return ok;
}

static void save_pdf(void)
{
    struct FileRequester *fr;
    char path[512], text[256];
    fr = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)"Save as PDF",
                                                       ASLFR_DoSaveMode, TRUE, ASLFR_InitialFile, (ULONG)base_name(R.pdf),
                                                       ASLFR_InitialPattern, (ULONG)"#?.pdf", ASLFR_DoPatterns, TRUE, TAG_END);
    if (!fr) {
        show_status("Not enough memory for the file requester");
        return;
    }
    if (AslRequestTags(fr, ASLFR_Window, (ULONG)R.win, ASLFR_SleepWindow, TRUE, TAG_END)) {
        copy(path, sizeof(path), fr->fr_Drawer);
        AddPart((STRPTR)path, fr->fr_File, sizeof(path));
        if (copy_file(R.pdf, path)) {
            snprintf(text, sizeof(text), "Saved %s", path);
            read_options(R.options);
            save_settings(R.options);
        } else
            snprintf(text, sizeof(text), "Couldn't save %s: check the disk isn't full or write-protected", path);
        show_status(text);
    }
    FreeAslRequest(fr);
}

static void print_now(void)
{
    static OAVRequest job;
    const OAPPrinter *p = chosen();
    char text[256];
    if (!p) {
        show_status("Choose a printer first, or use Find printers");
        return;
    }
    if (R.busy || R.sent)
        return;
    read_options(R.options);
    save_settings(R.options);
    if (oap_printer_is_file(p)) {
        save_pdf();
        return;
    }
    memset(&job, 0, sizeof(job));
    oav_layout_defaults(&job.layout);
    copy(job.source, sizeof(job.source), R.pdf);
    strcpy(job.action, "send");
    copy(job.uri, sizeof(job.uri), R.options->printer_uri);
    job.has_print_options = 1;
    job.print = *R.options;
    if (oav_submit(&job, R.request, sizeof(R.request), text, sizeof(text))) {
        R.busy = 1;
        R.percent = 0;
        rebuild();
        snprintf(text, sizeof(text), "Sending to %s...", p->name);
    }
    show_status(text);
}

static void find_printers(void)
{
    char path[256], cmd[300];
    BPTR in = Open((STRPTR)"NIL:", MODE_OLDFILE), out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    oap_program_path("OAPPrinters", path, sizeof(path));
    snprintf(cmd, sizeof(cmd), "\"%s\"", path);
    if (!in || !out || SystemTags((STRPTR)cmd, SYS_Asynch, TRUE, SYS_Input, in, SYS_Output, out, NP_StackSize, 65536, TAG_DONE) == -1) {
        if (in)
            Close(in);
        if (out)
            Close(out);
        show_status("Couldn't open Printers: install OAPPrinters beside OpenAmigaPrint or in C:");
        return;
    }
    show_status("Choose a printer in the Printers window and click Use for printing");
}

/* The worker's progress: "Uploading 42% (...)" moves the bar. */
static void poll_worker(void)
{
    char state[32], msg[256];
    const char *pct;
    if (!R.busy || !oav_result(R.request, state, sizeof(state), msg, sizeof(msg)))
        return;
    pct = strstr(msg, "Uploading ");
    if (pct) {
        R.percent = atoi(pct + 10);
        draw_progress();
    }
    show_status(msg);
    if (oav_result_terminal(state)) {
        R.busy = 0;
        R.finished = 1;
        /* a job accepted, or perhaps accepted, is never sent a second time from here */
        R.sent = !strcmp(state, "submitted") || !strcmp(state, "uncertain");
        if (!strcmp(state, "submitted"))
            R.percent = 100;
        rebuild();
        show_status(msg);
    }
}

static void stop_or_close(int *done)
{
    if (R.busy) {
        oav_cancel(R.request);
        show_status("Stopping the upload; waiting for the worker's answer. Don't send it again yet.");
    } else
        *done = 1;
}

static void cycle(UWORD id, int step)
{
    int *field = NULL, n = 2, v;
    switch (id) {
    case G_PRINTER: field = &R.row; n = R.offered_count; break;
    case G_SIDES: field = &R.sides; n = 3; break;
    case G_PAPER: v = R.options->paper == OAP_PAPER_LETTER; R.options->paper = (v ^ 1) ? OAP_PAPER_LETTER : OAP_PAPER_A4; break;
    case G_LAYOUT: v = R.options->orientation == OAP_LANDSCAPE; R.options->orientation = (v ^ 1) ? OAP_LANDSCAPE : OAP_PORTRAIT; break;
    case G_COLOUR: R.options->color = R.options->color ? OAP_MONO : OAP_COLOR; break;
    }
    if (field && n > 0)
        *field = (*field + step + n) % n;
    rebuild();
}

/* Keys: each control's underlined letter (Shift steps a choice back);
 * Return prints, Esc cancels. */
static void key(UWORD code, UWORD qual, int *done)
{
    int back = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? -1 : 1;
    if (code == '\r') {
        if (!R.busy && !R.sent)
            print_now();
        return;
    }
    if (code == 27) {
        stop_or_close(done);
        return;
    }
    switch (code | 0x20) {
    case 'p': if (!R.busy && !R.sent) print_now(); break;
    case 'd': if (!R.busy && !(chosen() && oap_printer_is_file(chosen()))) save_pdf(); break;
    case 'f': if (!R.busy) find_printers(); break;
    case 'c': stop_or_close(done); break;
    case 's':                                  /* _Stop while sending, _Save... for Save as PDF file */
        if (R.busy) stop_or_close(done);
        else if (chosen() && oap_printer_is_file(chosen())) print_now();
        break;
    case 'r': if (!R.busy) cycle(G_PRINTER, back); break;
    case 'z': cycle(G_PAPER, back); break;
    case 'l': cycle(G_LAYOUT, back); break;
    case 'i': cycle(G_SIDES, back); break;
    case 'u': cycle(G_COLOUR, back); break;
    case 'o': ActivateGadget(gad(G_COPIES), R.win, NULL); break;
    case 'a': keep_typing(); R.pages = 0; rebuild(); break;
    case 'g': keep_typing(); R.pages = 1; rebuild(); edit_range(); break;
    }
}

static int open_libraries(void)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 37);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 37);
    GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 37);
    AslBase = OpenLibrary((STRPTR)"asl.library", 38);
    return IntuitionBase && GfxBase && GadToolsBase && AslBase;
}

static void close_libraries(void)
{
    if (AslBase) CloseLibrary(AslBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    AslBase = GadToolsBase = NULL;
    GfxBase = NULL;
    IntuitionBase = NULL;
}

int oap_run_print_dialog(const char *pdf, OAPJobOptions *o)
{
    OAPSelectionListener selection;
    char title[160], picked[OAP_SELECTION_URI_MAX], start[OAP_SELECTION_URI_MAX];
    int done = 0, ok = 0;

    memset(&R, 0, sizeof(R));
    memset(&selection, 0, sizeof(selection));
    R.pdf = pdf;
    R.options = o;
    R.pdf_bytes = file_size(pdf);
    load_settings(o);
    job_page_setup(pdf, o);
    R.copies = o->copies >= 1 && o->copies <= 99 ? o->copies : 1;
    R.sides = o->duplex >= OAP_SIMPLEX && o->duplex <= OAP_DUPLEX_SHORT ? o->duplex : OAP_SIMPLEX;
    copy(R.range_text, sizeof(R.range_text), "1-1");
    copy(R.status_text, sizeof(R.status_text), R.pdf_bytes >= 0 ? "Ready" : "The document to print can't be read");
    if (!open_libraries() || !oap_gt_open(&R.g))
        goto out;
    snprintf(title, sizeof(title), "Print: %s", job_title(pdf));

    /* start on the printer the job asked for, else the default */
    copy(start, sizeof(start), o->printer_uri);
    if (!start[0] || strstr(start, "printer.local"))
        GetVar((STRPTR)"OpenAmigaPrint/PrinterURI", (STRPTR)start, sizeof(start), GVF_GLOBAL_ONLY);
    fill_printers(start);
    layout();
    if ((R.geo.inner_w + 24 > R.g.screen->Width || R.geo.inner_h + 40 > R.g.screen->Height) && oap_gt_fall_back(&R.g))
        layout();
    R.win = OpenWindowTags(NULL,
        WA_Title, (ULONG)title, WA_PubScreen, (ULONG)R.g.screen,
        WA_InnerWidth, R.geo.inner_w, WA_InnerHeight, R.geo.inner_h,
        WA_Left, (R.g.screen->Width - R.geo.inner_w) / 2, WA_Top, (R.g.screen->Height - R.geo.inner_h) / 2,
        WA_Activate, TRUE, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SmartRefresh, TRUE,
        WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW |
                  IDCMP_INTUITICKS | BUTTONIDCMP | CYCLEIDCMP | MXIDCMP | STRINGIDCMP | INTEGERIDCMP | TEXTIDCMP,
        TAG_DONE);
    if (!R.win)
        goto out;
    rebuild();
    oap_selection_open(&selection);
    ok = 1;

    while (!done) {
        struct IntuiMessage *im;
        Wait((1UL << R.win->UserPort->mp_SigBit) | oap_selection_mask(&selection) | SIGBREAKF_CTRL_C);
        if (oap_selection_receive(&selection, picked, sizeof(picked)) && !R.busy) {
            char text[200];
            fill_printers(picked);
            rebuild();
            snprintf(text, sizeof(text), "Printer chosen: %s", chosen() ? chosen()->name : picked);
            show_status(text);
        }
        while (!done && (im = GT_GetIMsg(R.win->UserPort)) != NULL) {
            ULONG class = im->Class;
            UWORD code = im->Code, qual = im->Qualifier;
            struct Gadget *g = (struct Gadget *)im->IAddress;
            GT_ReplyIMsg(im);
            switch (class) {
            case IDCMP_CLOSEWINDOW:
                if (R.busy)
                    oav_cancel(R.request);
                done = 1;
                break;
            case IDCMP_REFRESHWINDOW:
                GT_BeginRefresh(R.win);
                draw_static();
                GT_EndRefresh(R.win, TRUE);
                break;
            case IDCMP_INTUITICKS:
                if (++R.ticks >= 5) {
                    R.ticks = 0;
                    poll_worker();
                }
                break;
            case IDCMP_VANILLAKEY:
                key(code, qual, &done);
                break;
            case IDCMP_GADGETDOWN:
                if (g->GadgetID == G_PAGES) {
                    keep_typing();
                    R.pages = code;
                    rebuild();
                    if (R.pages)
                        edit_range();
                }
                break;
            case IDCMP_GADGETUP:
                switch (g->GadgetID) {
                case G_PRINTER: R.row = code; rebuild(); break;
                case G_FIND: find_printers(); break;
                case G_LESS: case G_MORE:
                    keep_typing();
                    R.copies += g->GadgetID == G_MORE ? 1 : -1;
                    if (R.copies < 1) R.copies = 1;
                    if (R.copies > 99) R.copies = 99;
                    GT_SetGadgetAttrs(gad(G_COPIES), R.win, NULL, GTIN_Number, R.copies, TAG_DONE);
                    break;
                case G_PAPER: R.options->paper = code ? OAP_PAPER_LETTER : OAP_PAPER_A4; draw_preview(); break;
                case G_LAYOUT: R.options->orientation = code ? OAP_LANDSCAPE : OAP_PORTRAIT; draw_preview(); break;
                case G_SIDES: R.sides = code; break;
                case G_COLOUR: R.options->color = code ? OAP_MONO : OAP_COLOR; break;
                case G_PRINT: print_now(); break;
                case G_SAVE: save_pdf(); break;
                case G_CANCEL: stop_or_close(&done); break;
                }
                break;
            }
        }
    }

out:
    oap_selection_close(&selection);
    if (R.win) {
        CloseWindow(R.win);
        R.win = NULL;
    }
    if (R.glist)
        FreeGadgets(R.glist);
    oap_gt_close(&R.g);
    close_libraries();
    return ok;
}

/* The queue lives in the Printers window now; this entry point opens it. */
int oap_run_queue_window(void)
{
    char path[256], cmd[300];
    oap_program_path("OAPPrinters", path, sizeof(path));
    snprintf(cmd, sizeof(cmd), "\"%s\" QUEUE", path);
    return SystemTags((STRPTR)cmd, SYS_Asynch, FALSE, TAG_DONE) == 0;
}
