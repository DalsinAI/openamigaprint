/* SPDX-License-Identifier: BSD-2-Clause */
/* OpenAmigaPrint's Printers and Queue window (C:OAPPrinters), in ReAction.
 * It replaces the separate printer browser and the GadTools queue window, as
 * the 3 October 2026 review set out:
 *   - printers by name, with their state and whether they take PDF; the
 *     address shows only for the printer selected;
 *   - discovery reports its progress and ends with a count or a reason;
 *   - printers found are remembered (ENV:OpenAmigaPrint/Printers), so the
 *     Print requester offers them without a new search;
 *   - one queue for every job, from printer.device and OpenAmigaView alike;
 *   - menus with Amiga-key shortcuts, and a key on every button.
 * Network work stays with C:OAPDiscover; only printers verified to take PDF
 * can be used for printing. */
#include "oap.h"
#include "oap_discovery.h"
#include "oap_printers.h"
#include "oap_queue.h"
#include "oap_selection.h"

#include <exec/types.h>
#include <exec/lists.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/listbrowser.h>
#include <gadgets/checkbox.h>
#include <gadgets/space.h>
#include <images/label.h>
#include <images/bevel.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/string.h>
#include <proto/listbrowser.h>
#include <proto/checkbox.h>
#include <proto/space.h>
#include <proto/label.h>
#include <reaction/reaction_macros.h>
#include <clib/alib_protos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned long __stack = 65536;
struct IntuitionBase *IntuitionBase;
struct Library *WindowBase, *LayoutBase, *ButtonBase, *StringBase, *ListBrowserBase, *CheckBoxBase, *LabelBase, *SpaceBase;

#define REG(r, t) register t __asm(#r)
#define QUEUE_MAX 48

enum {
    G_PRINTERS = 1, G_SEARCH, G_SHOWALL, G_USE, G_TEST, G_ADDRESS, G_CHECK, G_DETAILS, G_URI,
    G_QUEUE, G_QPRINT, G_QREMOVE, G_QREFRESH, G_STATUS,
    M_SEARCH = 100, M_ADD, M_QUIT, M_USE, M_TEST, M_QREFRESH, M_QPRINT, M_QREMOVE
};

typedef struct QueueRow {
    char pdf[256], name[64], state[48], printer[96], size[16];
} QueueRow;

static struct Printers {
    Object *window, *printers, *search, *showall, *use, *test, *address, *check, *details, *uri;
    Object *queue, *qprint, *qremove, *qrefresh, *status;
    struct Window *win;
    struct Screen *screen;
    struct Hook idcmp_hook;
    struct List printer_rows, queue_rows;
    OAPPrinterList known;
    OAPDiscovery scan;
    int row_printer[OAP_PRINTERS_MAX];          /* list row -> index in `known` */
    int rows;
    QueueRow jobs[QUEUE_MAX];
    int job_count;
    char output[160], status_text[256], details_text[256], uri_text[OAP_SELECTION_URI_MAX];
    char default_uri[OAP_SELECTION_URI_MAX];
    int scanning, show_all, ticks, poll, queue_ticks, generation, scan_polls;
    unsigned long last_found;
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

static struct ColumnInfo printer_columns[] = {
    { 56, (STRPTR)"Printer", 0 }, { 32, (STRPTR)"State", 0 }, { 12, (STRPTR)"PDF", 0 }, { -1, NULL, 0 }
};
static struct ColumnInfo queue_columns[] = {
    { 34, (STRPTR)"Document", 0 }, { 30, (STRPTR)"Printer", 0 }, { 24, (STRPTR)"State", 0 }, { 12, (STRPTR)"Size", 0 },
    { -1, NULL, 0 }
};

static void copy(char *dst, size_t cap, const char *src)
{
    if (!cap)
        return;
    strncpy(dst, src ? src : "", cap - 1);
    dst[cap - 1] = 0;
}

static ULONG get(Object *o, ULONG attr)
{
    ULONG v = 0;
    GetAttr(attr, o, &v);
    return v;
}

static void set(Object *o, Tag tag, ULONG value)
{
    if (P.win)
        SetGadgetAttrs((struct Gadget *)o, P.win, NULL, tag, value, TAG_DONE);
    else
        SetAttrs(o, tag, value, TAG_DONE);
}

static void show_status(const char *text)
{
    copy(P.status_text, sizeof(P.status_text), text);
    set(P.status, GA_Text, (ULONG)P.status_text);
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
    ULONG row = get(P.printers, LISTBROWSER_Selected);
    if ((int)row < 0 || (int)row >= P.rows)
        return NULL;
    return &P.known.printer[P.row_printer[row]];
}

static void show_details(void)
{
    OAPPrinter *p = selected_printer();
    int usable = p && p->pdf == OAP_PDF_YES && !P.scanning;
    if (!p) {
        copy(P.details_text, sizeof(P.details_text), "Select a printer to see what it can do.");
        P.uri_text[0] = 0;
    } else {
        snprintf(P.details_text, sizeof(P.details_text), "%s%s", p->note[0] ? p->note : state_word(p),
                 !strcmp(p->uri, P.default_uri) ? " (default)" : "");
        copy(P.uri_text, sizeof(P.uri_text), p->uri);
    }
    set(P.details, GA_Text, (ULONG)P.details_text);
    set(P.uri, STRINGA_TextVal, (ULONG)P.uri_text);
    set(P.use, GA_Disabled, !usable);
    set(P.test, GA_Disabled, !usable);
}

static void fill_printers(void)
{
    struct Node *n;
    int i, select = -1;
    char name[OAP_PRINTER_NAME_MAX + 12];
    OAPPrinter *was = selected_printer();
    char keep[OAP_SELECTION_URI_MAX];
    copy(keep, sizeof(keep), was ? was->uri : P.default_uri);
    set(P.printers, LISTBROWSER_Labels, (ULONG)~0);
    while ((n = RemHead(&P.printer_rows)))
        FreeListBrowserNode(n);
    P.rows = 0;
    for (i = 0; i < P.known.count && P.rows < OAP_PRINTERS_MAX; i++) {
        OAPPrinter *p = &P.known.printer[i];
        if (oap_printer_is_file(p) || (!P.show_all && p->pdf != OAP_PDF_YES))
            continue;
        snprintf(name, sizeof(name), "%s%s", p->name, !strcmp(p->uri, P.default_uri) ? " *" : "");
        n = AllocListBrowserNode(3, LBNA_Column, 0, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)name,
                                 LBNA_Column, 1, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)state_word(p),
                                 LBNA_Column, 2, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)pdf_word(p->pdf), TAG_DONE);
        if (!n)
            break;
        AddTail(&P.printer_rows, n);
        if (!strcmp(p->uri, keep))
            select = P.rows;
        P.row_printer[P.rows++] = i;
    }
    if (P.win)
        SetGadgetAttrs((struct Gadget *)P.printers, P.win, NULL, LISTBROWSER_Labels, (ULONG)&P.printer_rows,
                       LISTBROWSER_Selected, select, LISTBROWSER_MakeVisible, select < 0 ? 0 : select, TAG_DONE);
    else
        SetAttrs(P.printers, LISTBROWSER_Labels, (ULONG)&P.printer_rows, LISTBROWSER_Selected, select, TAG_DONE);
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
    set(P.search, GA_Disabled, TRUE);
    set(P.check, GA_Disabled, TRUE);
    show_status(uri ? "Asking that printer what it can print..." : "Searching the network for printers...");
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
        P.scanning = 0;
        set(P.search, GA_Disabled, FALSE);
        set(P.check, GA_Disabled, FALSE);
        oap_printers_save(&P.known);
        fill_printers();
        snprintf(text, sizeof(text), "The search didn't finish: no answer from the network in 45 seconds. "
                 "Check this Amiga's network is on, then Search again.");
        show_status(text);
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
    P.scanning = 0;
    DeleteFile((STRPTR)P.output);
    set(P.search, GA_Disabled, FALSE);
    set(P.check, GA_Disabled, FALSE);
    oap_printers_save(&P.known);
    fill_printers();
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
    fill_printers();
    snprintf(text, sizeof(text), "%s is now used for printing", p->name);
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

static void fill_queue(void)
{
    BPTR lock;
    struct FileInfoBlock *fib;
    struct Node *n;
    int i, select = (int)get(P.queue, LISTBROWSER_Selected);
    set(P.queue, LISTBROWSER_Labels, (ULONG)~0);
    while ((n = RemHead(&P.queue_rows)))
        FreeListBrowserNode(n);
    P.job_count = 0;
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
        n = AllocListBrowserNode(4, LBNA_Column, 0, LBNCA_Text, (ULONG)r->name, LBNA_Column, 1,
                                 LBNCA_Text, (ULONG)(r->printer[0] ? r->printer : "Not sent"), LBNA_Column, 2,
                                 LBNCA_Text, (ULONG)r->state, LBNA_Column, 3, LBNCA_Text, (ULONG)r->size, TAG_DONE);
        if (n)
            AddTail(&P.queue_rows, n);
    }
    if (select >= P.job_count)
        select = P.job_count - 1;
    set(P.queue, LISTBROWSER_Labels, (ULONG)&P.queue_rows);
    set(P.queue, LISTBROWSER_Selected, (ULONG)select);
    set(P.qprint, GA_Disabled, select < 0);
    set(P.qremove, GA_Disabled, select < 0);
}

static QueueRow *selected_job(void)
{
    int row = (int)get(P.queue, LISTBROWSER_Selected);
    return row >= 0 && row < P.job_count ? &P.jobs[row] : NULL;
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
    show_status(text);
    fill_queue();
}

/* ---- the window --------------------------------------------------------- */

static ULONG idcmp(REG(a0, struct Hook *h), REG(a2, Object *o), REG(a1, struct IntuiMessage *im))
{
    (void)h;
    (void)o;
    if (im->Class == IDCMP_INTUITICKS && ++P.ticks >= 5) {
        P.ticks = 0;
        P.poll = 1;
    }
    return 0;
}

static Object *button(const char *text, ULONG id)
{
    return NewObject(BUTTON_GetClass(), NULL, GA_ID, id, GA_RelVerify, TRUE, GA_Text, (ULONG)text, TAG_DONE);
}

static Object *text_line(ULONG id, const char *text, ULONG bevel)
{
    return NewObject(BUTTON_GetClass(), NULL, GA_ID, id, GA_ReadOnly, TRUE, BUTTON_BevelStyle, bevel,
                     BUTTON_Justification, BCJ_LEFT, GA_Text, (ULONG)text, TAG_DONE);
}

static Object *build_window(void)
{
    P.printers = NewObject(LISTBROWSER_GetClass(), NULL, GA_ID, G_PRINTERS, GA_RelVerify, TRUE,
                           LISTBROWSER_Labels, (ULONG)&P.printer_rows, LISTBROWSER_ColumnInfo, (ULONG)printer_columns,
                           LISTBROWSER_ColumnTitles, TRUE, LISTBROWSER_ShowSelected, TRUE, LISTBROWSER_MinVisible, 6,
                           TAG_DONE);
    P.search = button("_Search again", G_SEARCH);
    P.showall = NewObject(CHECKBOX_GetClass(), NULL, GA_ID, G_SHOWALL, GA_RelVerify, TRUE,
                          GA_Text, (ULONG)"Show _all printers", CHECKBOX_Checked, FALSE, TAG_DONE);
    P.details = text_line(G_DETAILS, "", BVS_NONE);
    P.uri = NewObject(STRING_GetClass(), NULL, GA_ID, G_URI, GA_ReadOnly, TRUE, STRINGA_MaxChars, OAP_SELECTION_URI_MAX,
                      STRINGA_TextVal, (ULONG)"", TAG_DONE);
    P.use = button("_Use for printing", G_USE);
    P.test = button("Print a _test page...", G_TEST);
    P.address = NewObject(STRING_GetClass(), NULL, GA_ID, G_ADDRESS, GA_RelVerify, TRUE, STRINGA_MaxChars, 383,
                          STRINGA_TextVal, (ULONG)"ipp://", TAG_DONE);
    P.check = button("_Check", G_CHECK);
    P.queue = NewObject(LISTBROWSER_GetClass(), NULL, GA_ID, G_QUEUE, GA_RelVerify, TRUE,
                        LISTBROWSER_Labels, (ULONG)&P.queue_rows, LISTBROWSER_ColumnInfo, (ULONG)queue_columns,
                        LISTBROWSER_ColumnTitles, TRUE, LISTBROWSER_ShowSelected, TRUE, LISTBROWSER_MinVisible, 5,
                        TAG_DONE);
    P.qprint = button("_Print...", G_QPRINT);
    P.qremove = button("_Remove", G_QREMOVE);
    P.qrefresh = button("Re_fresh", G_QREFRESH);
    P.status = text_line(G_STATUS, P.status_text, BVS_THIN);

    return NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)"OpenAmigaPrint: Printers and Queue", WA_PubScreen, (ULONG)P.screen, WA_Activate, TRUE,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
        WA_IDCMP, IDCMP_INTUITICKS | IDCMP_VANILLAKEY | IDCMP_MENUPICK,
        WA_InnerWidth, 680, WA_InnerHeight, 360,
        WINDOW_Position, WPOS_CENTERSCREEN, WINDOW_NewMenu, (ULONG)menus,
        WINDOW_IDCMPHook, (ULONG)&P.idcmp_hook, WINDOW_IDCMPHookBits, IDCMP_INTUITICKS,
        WINDOW_Layout, (ULONG)NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter, TRUE, LAYOUT_DeferLayout, TRUE,
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                    LAYOUT_BevelStyle, BVS_GROUP, LAYOUT_Label, (ULONG)"Printers on the network",
                    LAYOUT_AddChild, (ULONG)P.printers,
                    LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                        LAYOUT_AddChild, (ULONG)P.showall,
                        LAYOUT_AddChild, (ULONG)P.search, CHILD_WeightedWidth, 0, TAG_DONE), CHILD_WeightedHeight, 0,
                    TAG_DONE), CHILD_WeightedWidth, 60,
                LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                    LAYOUT_BevelStyle, BVS_GROUP, LAYOUT_Label, (ULONG)"Selected printer", LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, (ULONG)P.details, CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, (ULONG)P.uri, CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, (ULONG)P.use, CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, (ULONG)P.test, CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                        LAYOUT_BevelStyle, BVS_SBAR_VERT, LAYOUT_Label, (ULONG)"Add a printer by address",
                        LAYOUT_AddChild, (ULONG)P.address,
                        LAYOUT_AddChild, (ULONG)P.check, TAG_DONE), CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild, (ULONG)NewObject(SPACE_GetClass(), NULL, TAG_DONE),
                    TAG_DONE), CHILD_WeightedWidth, 40,
                TAG_DONE),
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                LAYOUT_BevelStyle, BVS_GROUP, LAYOUT_Label, (ULONG)"Queue",
                LAYOUT_AddChild, (ULONG)P.queue,
                LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                    LAYOUT_EvenSize, TRUE,
                    LAYOUT_AddChild, (ULONG)P.qprint,
                    LAYOUT_AddChild, (ULONG)P.qremove,
                    LAYOUT_AddChild, (ULONG)P.qrefresh, TAG_DONE), CHILD_WeightedHeight, 0,
                TAG_DONE),
            LAYOUT_AddChild, (ULONG)P.status, CHILD_WeightedHeight, 0,
            TAG_DONE),
        TAG_DONE);
}

static void action(ULONG id, int *done)
{
    switch (id) {
    case G_SEARCH: case M_SEARCH:
        begin_scan(NULL);
        break;
    case G_SHOWALL:
        P.show_all = get(P.showall, GA_Selected) != 0;
        fill_printers();
        break;
    case G_PRINTERS:
        show_details();
        break;
    case G_USE: case M_USE:
        use_printer();
        break;
    case G_TEST: case M_TEST:
        test_page();
        break;
    case M_ADD:
        ActivateGadget((struct Gadget *)P.address, P.win, NULL);
        break;
    case G_ADDRESS: case G_CHECK:
        begin_scan((const char *)get(P.address, STRINGA_TextVal));
        break;
    case G_QUEUE:
        set(P.qprint, GA_Disabled, selected_job() == NULL);
        set(P.qremove, GA_Disabled, selected_job() == NULL);
        break;
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

static int key(UWORD code, int *done)
{
    static const struct { char key; ULONG id; } keys[] = {
        { 's', G_SEARCH }, { 'u', G_USE }, { 't', G_TEST }, { 'c', G_CHECK }, { 'p', G_QPRINT }, { 'r', G_QREMOVE },
        { 'f', G_QREFRESH }
    };
    size_t i;
    if (code == 27) {
        *done = 1;
        return 1;
    }
    if ((code | 0x20) == 'a') {
        set(P.showall, GA_Selected, !get(P.showall, GA_Selected));
        action(G_SHOWALL, done);
        return 1;
    }
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        if ((code | 0x20) == keys[i].key) {
            Object *o = keys[i].id == G_SEARCH ? P.search : keys[i].id == G_USE ? P.use : keys[i].id == G_TEST ? P.test
                      : keys[i].id == G_CHECK ? P.check : keys[i].id == G_QPRINT ? P.qprint
                      : keys[i].id == G_QREMOVE ? P.qremove : P.qrefresh;
            if (!get(o, GA_Disabled))
                action(keys[i].id, done);
            return 1;
        }
    return 0;
}

int main(void)
{
    struct Node *n;
    ULONG sigs, result;
    UWORD code;
    int done = 0, rc = 20;

    memset(&P, 0, sizeof(P));
    NewList(&P.printer_rows);
    NewList(&P.queue_rows);
    P.idcmp_hook.h_Entry = (ULONG (*)())idcmp;
    copy(P.status_text, sizeof(P.status_text), "Ready");

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    WindowBase = OpenLibrary((STRPTR)"window.class", 44);
    LayoutBase = OpenLibrary((STRPTR)"gadgets/layout.gadget", 44);
    ButtonBase = OpenLibrary((STRPTR)"gadgets/button.gadget", 44);
    StringBase = OpenLibrary((STRPTR)"gadgets/string.gadget", 44);
    ListBrowserBase = OpenLibrary((STRPTR)"gadgets/listbrowser.gadget", 44);
    CheckBoxBase = OpenLibrary((STRPTR)"gadgets/checkbox.gadget", 44);
    LabelBase = OpenLibrary((STRPTR)"images/label.image", 44);
    SpaceBase = OpenLibrary((STRPTR)"gadgets/space.gadget", 44);
    if (!IntuitionBase || !WindowBase || !LayoutBase || !ButtonBase || !StringBase || !ListBrowserBase || !CheckBoxBase || !LabelBase || !SpaceBase)
        goto out;
    P.screen = LockPubScreen(NULL);
    if (!P.screen)
        goto out;
    load_default();
    oap_printers_load(&P.known);
    P.window = build_window();
    if (!P.window)
        goto out;
    fill_printers();
    fill_queue();
    P.win = RA_OpenWindow(P.window);
    if (!P.win)
        goto out;
    rc = 5;
    begin_scan(NULL);

    while (!done) {
        GetAttr(WINDOW_SigMask, P.window, &sigs);
        if (Wait(sigs | SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            done = 1;
        while ((result = RA_HandleInput(P.window, &code)) != WMHI_LASTMSG) {
            switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                done = 1;
                break;
            case WMHI_GADGETUP:
                action(result & WMHI_GADGETMASK, &done);
                break;
            case WMHI_VANILLAKEY:
                key(code, &done);
                break;
            case WMHI_MENUPICK: {
                struct Menu *strip = (struct Menu *)get(P.window, WINDOW_MenuStrip);
                UWORD number = (UWORD)(result & WMHI_MENUMASK);
                while (strip && number != MENUNULL) {
                    struct MenuItem *item = ItemAddress(strip, number);
                    if (!item)
                        break;
                    action((ULONG)GTMENUITEM_USERDATA(item), &done);
                    number = item->NextSelect;
                }
                break;
            }
            }
        }
        if (P.poll) {
            P.poll = 0;
            poll_scan();
            if (++P.queue_ticks >= 8) {            /* the queue: about every four seconds */
                P.queue_ticks = 0;
                fill_queue();
            }
        }
    }
    rc = 0;

out:
    if (P.window)
        DisposeObject(P.window);
    while ((n = RemHead(&P.printer_rows)))
        FreeListBrowserNode(n);
    while ((n = RemHead(&P.queue_rows)))
        FreeListBrowserNode(n);
    if (P.screen)
        UnlockPubScreen(NULL, P.screen);
    if (SpaceBase) CloseLibrary(SpaceBase);
    if (LabelBase) CloseLibrary(LabelBase);
    if (CheckBoxBase) CloseLibrary(CheckBoxBase);
    if (ListBrowserBase) CloseLibrary(ListBrowserBase);
    if (StringBase) CloseLibrary(StringBase);
    if (ButtonBase) CloseLibrary(ButtonBase);
    if (LayoutBase) CloseLibrary(LayoutBase);
    if (WindowBase) CloseLibrary(WindowBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return rc;
}
