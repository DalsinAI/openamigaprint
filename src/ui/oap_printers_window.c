/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* OpenAmigaPrint's Printers and Queue window (C:OAPPrinters), in GadTools
 * (Dale, 4 October 2026: OS 3.x applications use GadTools or MUI, not
 * ReAction). It replaces the separate printer browser and queue window, as
 * the 3 October review set out:
 *   - printers by name, with their state and whether they take PDF; the
 *     address shows only for the printer selected;
 *   - discovery reports its progress and ends with a count or a reason;
 *   - printers found are remembered (ENV:OpenAmigaPrint/Printers), so the
 *     Print requester offers them without a new search;
 *   - one queue for every job, from printer.device and OpenAmigaView alike;
 *   - menus with Amiga-key shortcuts, and a key on every button.
 * The lists use the system's fixed-width font so their columns line up.
 * Network work stays with C:OAPDiscover; only printers verified to take PDF
 * can be used for printing. */
#include "oap.h"
#include "oap_discovery.h"
#include "oap_gt.h"
#include "oap_printers.h"
#include "oap_stack.h"
#include "oap_queue.h"
#include "oap_selection.h"

#include <exec/types.h>
#include <exec/lists.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <graphics/gfxbase.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <clib/alib_protos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *GadToolsBase;
static const char oap_version[] __attribute__((used)) = "$VER: OAPPrinters 0.3 (4.10.2026)";

#define QUEUE_MAX 48
#define ROW_MAX 200

enum {
    G_PRINTERS = 1, G_SEARCH, G_SHOWALL, G_USE, G_TEST, G_ADDRESS, G_CHECK, G_DETAILS, G_URI,
    G_QUEUE, G_QPRINT, G_QREMOVE, G_QREFRESH, G_STATUS,
    M_SEARCH = 100, M_ADD, M_QUIT, M_USE, M_TEST, M_QREFRESH, M_QPRINT, M_QREMOVE
};

typedef struct QueueRow {
    char pdf[256], name[64], state[48], printer[96], size[16];
} QueueRow;

typedef struct Row {
    struct Node node;
    char text[ROW_MAX];
} Row;

/* Where everything goes, from the window's size and the fonts. */
typedef struct Geo {
    int lx, ly, lw, lh;                 /* Printers on the network */
    int rx, rw;                         /* Selected printer */
    int qy, qh;                         /* Queue */
    int plv_y, plv_h, qlv_y, qlv_h;     /* the two lists */
    int y_check, y_status;
    int pcols[3], qcols[4];             /* columns, in characters */
} Geo;

static struct Printers {
    OAPGT g;
    Geo geo;
    struct Window *win;
    struct Gadget *glist;
    struct Menu *menu;
    struct List printer_rows, queue_rows;
    Row prow[OAP_PRINTERS_MAX], qrow[QUEUE_MAX];
    OAPPrinterList known;
    OAPDiscovery scan;
    int row_printer[OAP_PRINTERS_MAX];          /* list row -> index in `known` */
    int rows, sel_printer;
    QueueRow jobs[QUEUE_MAX];
    int job_count, sel_job;
    ULONG click_secs, click_micros;
    int click_row;
    char output[160], status_text[256], details_text[256], uri_text[OAP_SELECTION_URI_MAX];
    char address[OAP_SELECTION_URI_MAX], default_uri[OAP_SELECTION_URI_MAX];
    int scanning, show_all, ticks, queue_ticks, generation, scan_polls;
    unsigned long last_found;
    int nat_w, nat_h, attached;
} P;

static struct NewMenu menus[] = {
    { NM_TITLE, (STRPTR)"Project", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Search again", (STRPTR)"R", 0, 0, (APTR)M_SEARCH },
    { NM_ITEM, (STRPTR)"Add printer by address...", (STRPTR)"A", 0, 0, (APTR)M_ADD },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)M_QUIT },
    { NM_TITLE, (STRPTR)"Printer", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Use for printing", (STRPTR)"U", 0, 0, (APTR)M_USE },
    { NM_ITEM, (STRPTR)"Print a test page...", (STRPTR)"T", 0, 0, (APTR)M_TEST },
    { NM_TITLE, (STRPTR)"Queue", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Print...", (STRPTR)"P", 0, 0, (APTR)M_QPRINT },
    { NM_ITEM, (STRPTR)"Remove from queue", NULL, 0, 0, (APTR)M_QREMOVE },
    { NM_ITEM, (STRPTR)"Refresh", (STRPTR)"F", 0, 0, (APTR)M_QREFRESH },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

static void copy(char *dst, size_t cap, const char *src)
{
    if (!cap)
        return;
    strncpy(dst, src ? src : "", cap - 1);
    dst[cap - 1] = 0;
}

/* A gadget of ours, only while the list is in the window. */
static struct Gadget *gad(UWORD id)
{
    return P.attached ? oap_gt_find(P.glist, id) : NULL;
}

static void set_disabled(UWORD id, int off)
{
    struct Gadget *g = gad(id);
    if (g && P.win)
        GT_SetGadgetAttrs(g, P.win, NULL, GA_Disabled, off, TAG_DONE);
}

static void set_text(UWORD id, const char *text)
{
    struct Gadget *g = gad(id);
    if (g && P.win)
        GT_SetGadgetAttrs(g, P.win, NULL, GTTX_Text, (ULONG)text, TAG_DONE);
}

static void show_status(const char *text)
{
    copy(P.status_text, sizeof(P.status_text), text);
    set_text(G_STATUS, P.status_text);
}

static int run_async(const char *cmd)
{
    BPTR in = Open((STRPTR)"NIL:", MODE_OLDFILE), out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (!in || !out || SystemTags((STRPTR)cmd, SYS_Asynch, TRUE, SYS_Input, in, SYS_Output, out, NP_StackSize, 65536, TAG_DONE) == -1) {
        if (in)
            Close(in);
        if (out)
            Close(out);
        return 0;
    }
    return 1;
}

/* A row of columns, each cut or padded to its width in characters. */
static void columns(char *out, size_t cap, const int *widths, int n, const char **cells)
{
    size_t used = 0;
    int c;
    out[0] = 0;
    for (c = 0; c < n && used + 1 < cap; c++) {
        int w = widths[c], i;
        const char *s = cells[c] ? cells[c] : "";
        for (i = 0; i < w && used + 1 < cap; i++)
            out[used++] = (i < w - 1 && *s) ? *s++ : ' ';
    }
    while (used && out[used - 1] == ' ')
        used--;
    out[used] = 0;
}

/* ---- printers ----------------------------------------------------------- */

static const char *pdf_word(int pdf)
{
    return pdf == OAP_PDF_YES ? "Yes" : pdf == OAP_PDF_NO ? "No" : "?";
}

static const char *state_word(const OAPPrinter *p)
{
    if (P.scanning && p->pdf == OAP_PDF_UNKNOWN)
        return "Checking...";
    if (p->pdf == OAP_PDF_YES)
        return "Ready";
    if (p->pdf == OAP_PDF_NO)
        return "Can't print PDF";
    if (!strncmp(p->uri, "ipps://", 7))
        return "Secure only (IPPS)";
    return "Not verified";
}

static OAPPrinter *selected_printer(void)
{
    if (P.sel_printer < 0 || P.sel_printer >= P.rows)
        return NULL;
    return &P.known.printer[P.row_printer[P.sel_printer]];
}

static int usable(void)
{
    OAPPrinter *p = selected_printer();
    return p && p->pdf == OAP_PDF_YES && !P.scanning;
}

static void show_details(void)
{
    OAPPrinter *p = selected_printer();
    if (!p) {
        copy(P.details_text, sizeof(P.details_text), "Select a printer to see what it can do.");
        P.uri_text[0] = 0;
    } else {
        snprintf(P.details_text, sizeof(P.details_text), "%s%s", p->note[0] ? p->note : state_word(p),
                 !strcmp(p->uri, P.default_uri) ? " (default)" : "");
        copy(P.uri_text, sizeof(P.uri_text), p->uri);
    }
    set_text(G_DETAILS, P.details_text);
    set_text(G_URI, P.uri_text);
    set_disabled(G_USE, !usable());
    set_disabled(G_TEST, !usable());
}

static void fill_printers(void)
{
    struct Gadget *lv = gad(G_PRINTERS);
    char name[OAP_PRINTER_NAME_MAX + 4];
    OAPPrinter *was = selected_printer();
    char keep[OAP_SELECTION_URI_MAX];
    int i;
    copy(keep, sizeof(keep), was ? was->uri : P.default_uri);
    if (lv && P.win)
        GT_SetGadgetAttrs(lv, P.win, NULL, GTLV_Labels, ~0UL, TAG_DONE);
    NewList(&P.printer_rows);
    P.rows = 0;
    P.sel_printer = -1;
    for (i = 0; i < P.known.count && P.rows < OAP_PRINTERS_MAX; i++) {
        OAPPrinter *p = &P.known.printer[i];
        const char *cells[3];
        Row *r = &P.prow[P.rows];
        if (oap_printer_is_file(p) || (!P.show_all && p->pdf != OAP_PDF_YES))
            continue;
        snprintf(name, sizeof(name), "%s%s", p->name, !strcmp(p->uri, P.default_uri) ? " *" : "");
        cells[0] = name;
        cells[1] = state_word(p);
        cells[2] = pdf_word(p->pdf);
        columns(r->text, sizeof(r->text), P.geo.pcols, 3, cells);
        r->node.ln_Name = r->text;
        AddTail(&P.printer_rows, &r->node);
        if (!strcmp(p->uri, keep))
            P.sel_printer = P.rows;
        P.row_printer[P.rows++] = i;
    }
    if (lv && P.win)
        GT_SetGadgetAttrs(lv, P.win, NULL, GTLV_Labels, (ULONG)&P.printer_rows, GTLV_Selected, (ULONG)P.sel_printer,
                          GTLV_Top, P.sel_printer < 0 ? 0 : P.sel_printer, TAG_DONE);
    show_details();
}

static void load_default(void)
{
    P.default_uri[0] = 0;
    GetVar((STRPTR)"OpenAmigaPrint/PrinterURI", (STRPTR)P.default_uri, sizeof(P.default_uri), GVF_GLOBAL_ONLY);
}

static int safe_uri(const char *s)
{
    size_t i, n = strlen(s);
    if (!n || n >= OAP_SELECTION_URI_MAX)
        return 0;
    for (i = 0; i < n; i++)
        if ((unsigned char)s[i] < 33 || s[i] == '"' || s[i] == '*' || s[i] == '\'')
            return 0;
    return !strncmp(s, "ipp://", 6) || !strncmp(s, "ipps://", 7);
}

static void begin_scan(const char *uri)
{
    char program[256], cmd[1024];
    if (P.scanning)
        return;
    if (uri && !safe_uri(uri)) {
        show_status("Type an address like ipp://printer.local:631/ipp/print, with no spaces");
        return;
    }
    snprintf(P.output, sizeof(P.output), "T:OAPPrinters-%08lx-%d.tsv", (unsigned long)FindTask(NULL), ++P.generation);
    oap_program_path("OAPDiscover", program, sizeof(program));
    if (uri)
        snprintf(cmd, sizeof(cmd), "\"%s\" --query \"%s\" \"%s\"", program, uri, P.output);
    else
        snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", program, P.output);
    if (!run_async(cmd)) {
        show_status("Couldn't start OAPDiscover: install it beside OAPPrinters or in C:");
        return;
    }
    P.scanning = 1;
    P.scan_polls = 0;
    P.last_found = 0;
    set_disabled(G_SEARCH, TRUE);
    set_disabled(G_CHECK, TRUE);
    show_status(uri ? "Asking that printer what it can print..." : "Searching the network for printers...");
    fill_printers();
}

static void end_scan(void)
{
    P.scanning = 0;
    set_disabled(G_SEARCH, FALSE);
    set_disabled(G_CHECK, FALSE);
    oap_printers_save(&P.known);
    fill_printers();
}

/* Each poll reads the worker's file: progress while it runs, then the end. */
static void poll_scan(void)
{
    char note[256], text[300];
    int done = 0;
    unsigned long found;
    size_t i;
    if (!P.scanning)
        return;
    if (++P.scan_polls > 90) {                 /* polls come about twice a second: 45 seconds */
        end_scan();
        show_status("The search didn't finish: no answer from the network in 45 seconds. "
                    "Check this Amiga's network is on, then Search again.");
        return;
    }
    if (!oap_discovery_load(P.output, &P.scan, &done, note, sizeof(note)))
        return;
    found = 0;
    for (i = 0; i < P.scan.count; i++)
        found += P.scan.printers[i].alive != 0;
    if (found != P.last_found || done) {
        P.last_found = found;
        oap_printers_merge(&P.known, &P.scan);
        fill_printers();
    }
    if (!done) {
        snprintf(text, sizeof(text), "Searching the network: %lu found so far...", found);
        show_status(text);
        return;
    }
    DeleteFile((STRPTR)P.output);
    end_scan();
    if (note[0])
        snprintf(text, sizeof(text), "Search finished: %lu found. %s", found, note);
    else
        snprintf(text, sizeof(text), "Search finished: %lu found", found);
    show_status(text);
}

static void use_printer(void)
{
    OAPPrinter *p = selected_printer();
    char error[256], text[300];
    if (!p || p->pdf != OAP_PDF_YES) {
        show_status("Only printers verified to take PDF can be used for printing");
        return;
    }
    if (!oap_preferences_store("OpenAmigaPrint/PrinterURI", p->uri, error, sizeof(error))) {
        show_status(error);
        return;
    }
    oap_printers_save(&P.known);
    oap_selection_publish(p->uri);
    copy(P.default_uri, sizeof(P.default_uri), p->uri);
    snprintf(text, sizeof(text), "%s is now used for printing", p->name);
    fill_printers();
    show_status(text);
}

static void test_page(void)
{
    OAPPrinter *p = selected_printer();
    char program[256], cmd[900];
    if (!p || p->pdf != OAP_PDF_YES)
        return;
    if (!oap_pdf_write_demo("T:OpenAmigaPrint-TestPage.pdf", "OpenAmigaPrint test page")) {
        show_status("Couldn't write the test page to T:");
        return;
    }
    oap_program_path("OpenAmigaPrint", program, sizeof(program));
    snprintf(cmd, sizeof(cmd), "\"%s\" \"T:OpenAmigaPrint-TestPage.pdf\" \"%s\"", program, p->uri);
    show_status(run_async(cmd) ? "The Print window shows the test page: check it, then click Print"
                               : "Couldn't open OpenAmigaPrint for the test page");
}

/* ---- the queue ---------------------------------------------------------- */

static void job_field(const char *line, const char *key, char *dst, size_t cap)
{
    size_t n = strlen(key);
    if (!strncmp(line, key, n)) {
        copy(dst, cap, line + n);
        dst[strcspn(dst, "\r\n")] = 0;
    }
}

static void read_job(QueueRow *r)
{
    char path[270], line[600], printer[OAP_SELECTION_URI_MAX] = "", state[48] = "queued";
    FILE *f;
    size_t n = strlen(r->pdf);
    int i;
    if (n > 4 && n < sizeof(path)) {
        strcpy(path, r->pdf);
        strcpy(path + n - 4, ".job");
        f = fopen(path, "r");
        if (f) {
            while (fgets(line, sizeof(line), f)) {
                job_field(line, "state=", state, sizeof(state));
                job_field(line, "printer=", printer, sizeof(printer));
                job_field(line, "title=", r->name, sizeof(r->name));   /* what was printed, not the spool name */
            }
            fclose(f);
        }
    }
    copy(r->state, sizeof(r->state), !strcmp(state, "queued") ? "Waiting" : !strcmp(state, "submitted") ? "Sent"
                                      : !strcmp(state, "uncertain") ? "Unknown: check the printer"
                                      : !strcmp(state, "uploading") ? "Sending" : !strcmp(state, "error") ? "Failed"
                                      : !strcmp(state, "cancelled") ? "Stopped" : state);
    r->printer[0] = 0;
    for (i = 0; printer[0] && i < P.known.count; i++)
        if (!strcmp(P.known.printer[i].uri, printer))
            copy(r->printer, sizeof(r->printer), P.known.printer[i].name);
    if (!r->printer[0] && printer[0])
        oap_printer_name_from_uri(printer, r->printer, sizeof(r->printer));
}

static QueueRow *selected_job(void)
{
    return P.sel_job >= 0 && P.sel_job < P.job_count ? &P.jobs[P.sel_job] : NULL;
}

static void fill_queue(void)
{
    struct Gadget *lv = gad(G_QUEUE);
    BPTR lock;
    struct FileInfoBlock *fib;
    char keep[256];
    int i;
    copy(keep, sizeof(keep), selected_job() ? selected_job()->pdf : "");
    if (lv && P.win)
        GT_SetGadgetAttrs(lv, P.win, NULL, GTLV_Labels, ~0UL, TAG_DONE);
    NewList(&P.queue_rows);
    P.job_count = 0;
    P.sel_job = -1;
    lock = Lock((STRPTR)OAP_QUEUE_DIR, ACCESS_READ);
    fib = AllocDosObject(DOS_FIB, NULL);
    if (lock && fib && Examine(lock, fib)) {
        while (P.job_count < QUEUE_MAX && ExNext(lock, fib)) {
            QueueRow *r = &P.jobs[P.job_count];
            size_t len = strlen(fib->fib_FileName);
            if (fib->fib_DirEntryType > 0 || len < 5 || strcmp(fib->fib_FileName + len - 4, ".pdf"))
                continue;
            snprintf(r->pdf, sizeof(r->pdf), "%s/%s", OAP_QUEUE_DIR, fib->fib_FileName);
            snprintf(r->name, sizeof(r->name), "%.*s", (int)(len - 4), fib->fib_FileName);
            if (fib->fib_Size >= 1024L * 1024L)
                snprintf(r->size, sizeof(r->size), "%ld.%ld MB", (long)fib->fib_Size / 1048576L, (long)fib->fib_Size % 1048576L / 104858L);
            else
                snprintf(r->size, sizeof(r->size), "%ld KB", (long)(fib->fib_Size + 1023) / 1024);
            read_job(r);
            P.job_count++;
        }
    }
    if (fib)
        FreeDosObject(DOS_FIB, fib);
    if (lock)
        UnLock(lock);
    for (i = 0; i < P.job_count; i++) {
        QueueRow *r = &P.jobs[i];
        const char *cells[4];
        cells[0] = r->name;
        cells[1] = r->printer[0] ? r->printer : "Not sent";
        cells[2] = r->state;
        cells[3] = r->size;
        columns(P.qrow[i].text, sizeof(P.qrow[i].text), P.geo.qcols, 4, cells);
        P.qrow[i].node.ln_Name = P.qrow[i].text;
        AddTail(&P.queue_rows, &P.qrow[i].node);
        if (keep[0] && !strcmp(r->pdf, keep))
            P.sel_job = i;
    }
    if (lv && P.win)
        GT_SetGadgetAttrs(lv, P.win, NULL, GTLV_Labels, (ULONG)&P.queue_rows, GTLV_Selected, (ULONG)P.sel_job, TAG_DONE);
    set_disabled(G_QPRINT, P.sel_job < 0);
    set_disabled(G_QREMOVE, P.sel_job < 0);
}

static void print_job(void)
{
    QueueRow *r = selected_job();
    char program[256], cmd[600];
    if (!r)
        return;
    oap_program_path("OpenAmigaPrint", program, sizeof(program));
    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", program, r->pdf);
    show_status(run_async(cmd) ? "The Print window is open for that document" : "Couldn't open OpenAmigaPrint");
}

/* Removing keeps the files: they move to the queue's Removed drawer. */
static void remove_job(void)
{
    QueueRow *r = selected_job();
    char dir[200], to[400], from_job[270], to_job[400], text[300];
    BPTR lock;
    size_t n;
    if (!r)
        return;
    snprintf(dir, sizeof(dir), "%s/Removed", OAP_QUEUE_DIR);
    lock = CreateDir((STRPTR)dir);
    if (lock)
        UnLock(lock);
    snprintf(to, sizeof(to), "%s/%s", dir, FilePart((STRPTR)r->pdf));
    n = strlen(r->pdf);
    copy(from_job, sizeof(from_job), r->pdf);
    if (n > 4)
        strcpy(from_job + n - 4, ".job");
    copy(to_job, sizeof(to_job), to);
    n = strlen(to_job);
    if (n > 4)
        strcpy(to_job + n - 4, ".job");
    if (!Rename((STRPTR)r->pdf, (STRPTR)to)) {
        show_status("Couldn't move the document out of the queue");
        return;
    }
    Rename((STRPTR)from_job, (STRPTR)to_job);
    snprintf(text, sizeof(text), "%s moved to %s", r->name, dir);
    P.sel_job = -1;
    fill_queue();
    show_status(text);
}

/* ---- the window --------------------------------------------------------- */

static void layout(int iw, int ih)
{
    OAPGT *g = &P.g;
    Geo *G = &P.geo;
    int gh = g->gad_h, fl = g->fixed->tf_YSize, right_h, chars, rest;
    G->lx = OAP_GT_MARGIN;
    G->ly = OAP_GT_MARGIN;
    G->lw = (iw - 2 * OAP_GT_MARGIN - OAP_GT_GAP) * 58 / 100;
    G->rx = G->lx + G->lw + OAP_GT_GAP;
    G->rw = iw - OAP_GT_MARGIN - G->rx;
    right_h = g->fh + OAP_GT_GAP + 2 * g->line_h + OAP_GT_GAP + gh + OAP_GT_GAP + 2 * gh + 4 + OAP_GT_GAP + g->line_h + gh + OAP_GT_GAP;
    G->plv_y = G->ly + g->fh + OAP_GT_GAP + fl + 2;
    G->plv_h = 6 * fl + 4;
    G->y_check = G->plv_y + G->plv_h + OAP_GT_GAP;
    G->lh = G->y_check + gh + OAP_GT_GAP - G->ly;
    if (right_h > G->lh) {
        G->plv_h += right_h - G->lh;
        G->y_check += right_h - G->lh;
        G->lh = right_h;
    }
    G->y_status = ih - OAP_GT_MARGIN - gh;
    G->qy = G->ly + G->lh + OAP_GT_GAP;
    G->qh = G->y_status - OAP_GT_GAP - G->qy;
    G->qlv_y = G->qy + g->fh + OAP_GT_GAP + fl + 2;
    G->qlv_h = G->qy + G->qh - OAP_GT_GAP - gh - OAP_GT_GAP - G->qlv_y;
    /* columns, in characters of the fixed font: the list's width less its scroller */
    chars = (G->lw - 2 * OAP_GT_INSET - 22) / g->fixed_w;
    G->pcols[2] = 4;
    G->pcols[1] = 19;
    G->pcols[0] = (rest = chars - 23) > 8 ? rest : 8;
    chars = (iw - 2 * OAP_GT_MARGIN - 2 * OAP_GT_INSET - 22) / g->fixed_w;
    G->qcols[3] = 9;
    G->qcols[2] = chars * 22 / 100 > 12 ? chars * 22 / 100 : 12;
    G->qcols[1] = chars * 28 / 100 > 12 ? chars * 28 / 100 : 12;
    G->qcols[0] = (rest = chars - G->qcols[1] - G->qcols[2] - G->qcols[3]) > 10 ? rest : 10;
}

static void natural_size(void)
{
    OAPGT *g = &P.g;
    int left = 52 * g->fixed_w + 2 * OAP_GT_INSET + 22;      /* room for a long printer name */
    int right = oap_gt_text_w(g, "Add a printer by address") + 2 * OAP_GT_INSET + 40;
    int w;
    if ((w = oap_gt_text_w(g, "Print a _test page...") + 2 * OAP_GT_INSET + 32) > right)
        right = w;
    P.nat_w = 2 * OAP_GT_MARGIN + OAP_GT_GAP + (left * 100 / 58 > left + right ? left * 100 / 58 : left + right);
    layout(P.nat_w, 1000);
    P.nat_h = P.geo.qy - 0 + g->fh + OAP_GT_GAP + g->fixed->tf_YSize + 2 + 5 * g->fixed->tf_YSize + 4 +
              OAP_GT_GAP + g->gad_h + OAP_GT_GAP + OAP_GT_GAP + g->gad_h + OAP_GT_MARGIN;
}

static void build(void)
{
    OAPGT *g = &P.g;
    Geo *G = &P.geo;
    struct Gadget *p;
    int bx = P.win->BorderLeft, by = P.win->BorderTop, gh = g->gad_h, iw, bw, x, cw;
    iw = P.win->Width - P.win->BorderLeft - P.win->BorderRight;
    layout(iw, P.win->Height - P.win->BorderTop - P.win->BorderBottom);
    P.glist = NULL;
    p = CreateContext(&P.glist);
    /* printers */
    p = CreateGadget(LISTVIEW_KIND, p, oap_gt_ng(g, bx + G->lx + OAP_GT_INSET, by + G->plv_y, G->lw - 2 * OAP_GT_INSET, G->plv_h,
                     NULL, G_PRINTERS, 0, 1), GTLV_Labels, (ULONG)&P.printer_rows, GTLV_ShowSelected, 0UL,
                     GTLV_Selected, (ULONG)P.sel_printer, TAG_DONE);
    bw = oap_gt_text_w(g, "_Search again") + 16;
    p = CreateGadget(CHECKBOX_KIND, p, oap_gt_ng(g, bx + G->lx + OAP_GT_INSET, by + G->y_check + (gh - 11) / 2, 26, 11,
                     "Show _all printers", G_SHOWALL, PLACETEXT_RIGHT, 0), GTCB_Checked, P.show_all, GT_Underscore, '_', TAG_DONE);
    p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, bx + G->lx + G->lw - OAP_GT_INSET - bw, by + G->y_check, bw, gh, "_Search again",
                     G_SEARCH, PLACETEXT_IN, 0), GT_Underscore, '_', GA_Disabled, P.scanning, TAG_DONE);
    /* the selected printer */
    x = G->rx + OAP_GT_INSET;
    cw = G->rw - 2 * OAP_GT_INSET;
    {
        int y = G->ly + g->fh + OAP_GT_GAP;
        p = CreateGadget(TEXT_KIND, p, oap_gt_ng(g, bx + x, by + y, cw, 2 * g->line_h, NULL, G_DETAILS, 0, 0),
                         GTTX_Text, (ULONG)P.details_text, GTTX_CopyText, TRUE, TAG_DONE);
        y += 2 * g->line_h + OAP_GT_GAP;
        p = CreateGadget(TEXT_KIND, p, oap_gt_ng(g, bx + x, by + y, cw, gh, NULL, G_URI, 0, 0),
                         GTTX_Text, (ULONG)P.uri_text, GTTX_Border, TRUE, GTTX_CopyText, TRUE, TAG_DONE);
        y += gh + OAP_GT_GAP;
        p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, bx + x, by + y, cw, gh, "_Use for printing", G_USE, PLACETEXT_IN, 0),
                         GT_Underscore, '_', GA_Disabled, !usable(), TAG_DONE);
        y += gh + 4;
        p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, bx + x, by + y, cw, gh, "Print a _test page...", G_TEST, PLACETEXT_IN, 0),
                         GT_Underscore, '_', GA_Disabled, !usable(), TAG_DONE);
        y += gh + OAP_GT_GAP + g->line_h;           /* under "Add a printer by address" */
        bw = oap_gt_text_w(g, "_Check") + 16;
        p = CreateGadget(STRING_KIND, p, oap_gt_ng(g, bx + x, by + y, cw - bw - 4, gh, NULL, G_ADDRESS, 0, 0),
                         GTST_String, (ULONG)P.address, GTST_MaxChars, OAP_SELECTION_URI_MAX - 1, TAG_DONE);
        p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, bx + x + cw - bw, by + y, bw, gh, "_Check", G_CHECK, PLACETEXT_IN, 0),
                         GT_Underscore, '_', GA_Disabled, P.scanning, TAG_DONE);
    }
    /* the queue */
    cw = iw - 2 * OAP_GT_MARGIN - 2 * OAP_GT_INSET;
    p = CreateGadget(LISTVIEW_KIND, p, oap_gt_ng(g, bx + OAP_GT_MARGIN + OAP_GT_INSET, by + G->qlv_y, cw, G->qlv_h, NULL, G_QUEUE, 0, 1),
                     GTLV_Labels, (ULONG)&P.queue_rows, GTLV_ShowSelected, 0UL, GTLV_Selected, (ULONG)P.sel_job, TAG_DONE);
    bw = (cw - 2 * OAP_GT_GAP) / 3;
    {
        int y = G->qy + G->qh - OAP_GT_GAP - gh, x0 = OAP_GT_MARGIN + OAP_GT_INSET;
        p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, bx + x0, by + y, bw, gh, "_Print...", G_QPRINT, PLACETEXT_IN, 0),
                         GT_Underscore, '_', GA_Disabled, P.sel_job < 0, TAG_DONE);
        p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, bx + x0 + bw + OAP_GT_GAP, by + y, bw, gh, "_Remove", G_QREMOVE, PLACETEXT_IN, 0),
                         GT_Underscore, '_', GA_Disabled, P.sel_job < 0, TAG_DONE);
        p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, bx + x0 + 2 * (bw + OAP_GT_GAP), by + y, cw - 2 * (bw + OAP_GT_GAP), gh, "Re_fresh",
                         G_QREFRESH, PLACETEXT_IN, 0), GT_Underscore, '_', TAG_DONE);
    }
    p = CreateGadget(TEXT_KIND, p, oap_gt_ng(g, bx + OAP_GT_MARGIN, by + G->y_status, iw - 2 * OAP_GT_MARGIN, gh, NULL, G_STATUS, 0, 0),
                     GTTX_Text, (ULONG)P.status_text, GTTX_Border, TRUE, GTTX_CopyText, TRUE, TAG_DONE);
    (void)p;
}

static void header(int x, int y, const int *widths, int n, const char **titles)
{
    char text[ROW_MAX];
    struct RastPort *rp = P.win->RPort;
    columns(text, sizeof(text), widths, n, titles);
    SetFont(rp, P.g.fixed);
    SetAPen(rp, oap_gt_pen(&P.g, HIGHLIGHTTEXTPEN));
    SetDrMd(rp, JAM1);
    Move(rp, x + 4, y + P.g.fixed->tf_Baseline);
    Text(rp, (STRPTR)text, strlen(text));
    SetFont(rp, P.g.font);
}

static void draw_static(void)
{
    static const char *ptitles[] = { "Printer", "State", "PDF" };
    static const char *qtitles[] = { "Document", "Printer", "State", "Size" };
    OAPGT *g = &P.g;
    Geo *G = &P.geo;
    struct RastPort *rp = P.win->RPort;
    int bx = P.win->BorderLeft, by = P.win->BorderTop, iw = P.win->Width - P.win->BorderLeft - P.win->BorderRight;
    int fl = g->fixed->tf_YSize;
    oap_gt_group(g, rp, bx + G->lx, by + G->ly, G->lw, G->lh, "Printers on the network");
    header(bx + G->lx + OAP_GT_INSET, by + G->plv_y - fl - 2, G->pcols, 3, ptitles);
    oap_gt_group(g, rp, bx + G->rx, by + G->ly, G->rw, G->lh, "Selected printer");
    oap_gt_text(g, rp, bx + G->rx + OAP_GT_INSET,
                by + G->ly + g->fh + OAP_GT_GAP + 2 * g->line_h + OAP_GT_GAP + 3 * g->gad_h + 4 + 2 * OAP_GT_GAP,
                "Add a printer by address", TEXTPEN, G->rw - 2 * OAP_GT_INSET);
    oap_gt_group(g, rp, bx + OAP_GT_MARGIN, by + G->qy, iw - 2 * OAP_GT_MARGIN, G->qh, "Queue");
    header(bx + OAP_GT_MARGIN + OAP_GT_INSET, by + G->qlv_y - fl - 2, G->qcols, 4, qtitles);
}

/* Builds the gadgets for the window's size and draws everything again. */
static void rebuild(void)
{
    struct Gadget *a = gad(G_ADDRESS);         /* what was typed, kept across the rebuild */
    if (a)
        copy(P.address, sizeof(P.address), (char *)((struct StringInfo *)a->SpecialInfo)->Buffer);
    if (P.glist) {
        RemoveGList(P.win, P.glist, -1);
        P.attached = 0;
        FreeGadgets(P.glist);
        P.glist = NULL;
    }
    oap_gt_erase(P.win);
    /* the rows follow the columns' widths for this size; with no gadgets in
     * the window yet nothing is set on them (GadTools may only change
     * gadgets that are in their window: Intuition loops looking otherwise) */
    layout(P.win->Width - P.win->BorderLeft - P.win->BorderRight, P.win->Height - P.win->BorderTop - P.win->BorderBottom);
    fill_printers();
    fill_queue();
    build();
    if (!P.glist)
        return;
    AddGList(P.win, P.glist, ~0, -1, NULL);
    P.attached = 1;
    RefreshGList(P.glist, P.win, NULL, -1);
    GT_RefreshWindow(P.win, NULL);
    draw_static();
}

static void action(ULONG id, UWORD code, int *done)
{
    switch (id) {
    case G_SEARCH: case M_SEARCH:
        begin_scan(NULL);
        break;
    case G_SHOWALL:
        P.show_all = (gad(G_SHOWALL)->Flags & GFLG_SELECTED) != 0;
        fill_printers();
        break;
    case G_PRINTERS:
        P.sel_printer = code;
        show_details();
        break;
    case G_USE: case M_USE:
        use_printer();
        break;
    case G_TEST: case M_TEST:
        test_page();
        break;
    case M_ADD:
        ActivateGadget(gad(G_ADDRESS), P.win, NULL);
        break;
    case G_ADDRESS: case G_CHECK:
        copy(P.address, sizeof(P.address), (char *)((struct StringInfo *)gad(G_ADDRESS)->SpecialInfo)->Buffer);
        begin_scan(P.address);
        break;
    case G_QUEUE: {
        ULONG secs, micros;
        CurrentTime(&secs, &micros);
        if ((int)code == P.click_row && DoubleClick(P.click_secs, P.click_micros, secs, micros)) {
            P.sel_job = code;
            print_job();                     /* a double-click opens it in the Print window */
            P.click_row = -1;
            break;
        }
        P.click_row = code;
        P.click_secs = secs;
        P.click_micros = micros;
        P.sel_job = code;
        set_disabled(G_QPRINT, FALSE);
        set_disabled(G_QREMOVE, FALSE);
        break;
    }
    case G_QPRINT: case M_QPRINT:
        print_job();
        break;
    case G_QREMOVE: case M_QREMOVE:
        remove_job();
        break;
    case G_QREFRESH: case M_QREFRESH:
        fill_queue();
        break;
    case M_QUIT:
        *done = 1;
        break;
    }
}

static void key(UWORD code, int *done)
{
    static const struct { char key; ULONG id; } keys[] = {
        { 's', G_SEARCH }, { 'u', G_USE }, { 't', G_TEST }, { 'c', G_CHECK }, { 'p', G_QPRINT }, { 'r', G_QREMOVE },
        { 'f', G_QREFRESH }
    };
    size_t i;
    if (code == 27) {
        *done = 1;
        return;
    }
    if ((code | 0x20) == 'a') {
        P.show_all = !P.show_all;
        GT_SetGadgetAttrs(gad(G_SHOWALL), P.win, NULL, GTCB_Checked, P.show_all, TAG_DONE);
        fill_printers();
        return;
    }
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        if ((code | 0x20) == keys[i].key) {
            struct Gadget *g = gad((UWORD)keys[i].id);
            if (g && !(g->Flags & GFLG_DISABLED))
                action(keys[i].id, 0, done);
            return;
        }
}

static int printers_main(int argc, char **argv)
{
    int done = 0, rc = 20;
    (void)argc;
    (void)argv;

    memset(&P, 0, sizeof(P));
    NewList(&P.printer_rows);
    NewList(&P.queue_rows);
    P.sel_printer = P.sel_job = P.click_row = -1;
    copy(P.status_text, sizeof(P.status_text), "Ready");
    copy(P.address, sizeof(P.address), "ipp://");

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 37);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 37);
    GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 37);
    if (!IntuitionBase || !GfxBase || !GadToolsBase || !oap_gt_open(&P.g))
        goto out;
    load_default();
    oap_printers_load(&P.known);
    natural_size();
    if ((P.nat_w + 24 > P.g.screen->Width || P.nat_h + 40 > P.g.screen->Height) && oap_gt_fall_back(&P.g))
        natural_size();
    P.menu = CreateMenus(menus, TAG_DONE);
    if (P.menu)
        LayoutMenus(P.menu, P.g.vi, GTMN_NewLookMenus, TRUE, TAG_DONE);
    P.win = OpenWindowTags(NULL,
        WA_Title, (ULONG)"OpenAmigaPrint: Printers and Queue", WA_PubScreen, (ULONG)P.g.screen,
        WA_InnerWidth, P.nat_w, WA_InnerHeight, P.nat_h,
        WA_Left, (P.g.screen->Width - P.nat_w) / 2, WA_Top, (P.g.screen->Height - P.nat_h) / 2,
        WA_Activate, TRUE, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
        WA_SizeBBottom, TRUE, WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_MENUPICK | IDCMP_VANILLAKEY |
                  IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE | IDCMP_INTUITICKS | LISTVIEWIDCMP | BUTTONIDCMP |
                  CHECKBOXIDCMP | STRINGIDCMP | TEXTIDCMP,
        TAG_DONE);
    if (!P.win)
        goto out;
    WindowLimits(P.win, P.win->Width, P.win->Height, ~0, ~0);
    if (P.menu)
        SetMenuStrip(P.win, P.menu);
    rebuild();
    rc = 5;
    begin_scan(NULL);

    while (!done) {
        struct IntuiMessage *im;
        if (Wait((1UL << P.win->UserPort->mp_SigBit) | SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            done = 1;
        while (!done && (im = GT_GetIMsg(P.win->UserPort)) != NULL) {
            ULONG class = im->Class;
            UWORD code = im->Code;
            struct Gadget *g = (struct Gadget *)im->IAddress;
            GT_ReplyIMsg(im);
            switch (class) {
            case IDCMP_CLOSEWINDOW:
                done = 1;
                break;
            case IDCMP_REFRESHWINDOW:
                GT_BeginRefresh(P.win);
                draw_static();
                GT_EndRefresh(P.win, TRUE);
                break;
            case IDCMP_NEWSIZE:
                rebuild();
                break;
            case IDCMP_INTUITICKS:
                if (++P.ticks >= 5) {
                    P.ticks = 0;
                    poll_scan();
                    if (++P.queue_ticks >= 8) {    /* the queue: about every four seconds */
                        P.queue_ticks = 0;
                        fill_queue();
                    }
                }
                break;
            case IDCMP_GADGETUP:
                action(g->GadgetID, code, &done);
                break;
            case IDCMP_VANILLAKEY:
                key(code, &done);
                break;
            case IDCMP_MENUPICK: {
                UWORD number = code;
                while (P.menu && number != MENUNULL && !done) {
                    struct MenuItem *item = ItemAddress(P.menu, number);
                    if (!item)
                        break;
                    action((ULONG)GTMENUITEM_USERDATA(item), 0, &done);
                    number = item->NextSelect;
                }
                break;
            }
            }
        }
    }
    rc = 0;

out:
    if (P.win) {
        ClearMenuStrip(P.win);
        CloseWindow(P.win);
    }
    if (P.glist)
        FreeGadgets(P.glist);
    if (P.menu)
        FreeMenus(P.menu);
    oap_gt_close(&P.g);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return rc;
}

int main(int argc, char **argv)
{
    return oap_main_with_stack(printers_main, argc, argv, 65536);
}
