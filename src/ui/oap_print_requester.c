/* SPDX-License-Identifier: BSD-2-Clause */
/* OpenAmigaPrint's Print requester, in ReAction. It replaces the GadTools
 * window of src/amiga/ui.c and keeps its contract, oap_run_print_dialog(),
 * as the 3 October 2026 review set out:
 *   - the printer is chosen by name from one list that also offers
 *     "Save as PDF file"; only printers verified to take PDF are offered;
 *   - copies, pages, paper, layout, sides and colour are labelled gadgets;
 *   - the preview is drawn to the paper's real shape;
 *   - progress shows in a status line and a fuel gauge;
 *   - layout gadgets follow the screen's font, the window opens centred,
 *     every control has a key, and the last settings are remembered.
 * Sending stays with C:OAVWorker, and its rules stay: an upload whose outcome
 * is uncertain keeps Print disabled so a job is never sent twice. */
#include "oap.h"
#include "oap_discovery.h"
#include "oap_printers.h"
#include "oap_selection.h"
#include "oav_jobs.h"

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/gadgetclass.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <libraries/asl.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/chooser.h>
#include <gadgets/integer.h>
#include <gadgets/radiobutton.h>
#include <gadgets/string.h>
#include <gadgets/fuelgauge.h>
#include <gadgets/space.h>
#include <images/label.h>
#include <images/bevel.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/asl.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/chooser.h>
#include <proto/integer.h>
#include <proto/radiobutton.h>
#include <proto/string.h>
#include <proto/fuelgauge.h>
#include <proto/space.h>
#include <proto/label.h>
#include <reaction/reaction_macros.h>
#include <clib/alib_protos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *AslBase, *WindowBase, *LayoutBase, *ButtonBase, *ChooserBase, *IntegerBase,
    *RadioButtonBase, *StringBase, *FuelGaugeBase, *SpaceBase, *LabelBase;

#define REG(r, t) register t __asm(#r)
#define SETTINGS_VAR "OpenAmigaPrint/PrintSettings"

enum {
    G_PRINTER = 1, G_FIND, G_COPIES, G_PAGES, G_RANGE, G_PAPER, G_LAYOUT, G_SIDES, G_COLOUR,
    G_PRINT, G_SAVE, G_CANCEL, G_PREVIEW, G_INFO, G_STATUS, G_FUEL
};

static STRPTR paper_labels[] = { (STRPTR)"A4 (210 \xd7 297 mm)", (STRPTR)"Letter (8.5 \xd7 11 in)", NULL };
static STRPTR layout_labels[] = { (STRPTR)"Portrait", (STRPTR)"Landscape", NULL };
static STRPTR sides_labels[] = { (STRPTR)"One-sided", (STRPTR)"Two-sided, long edge", (STRPTR)"Two-sided, short edge", NULL };
static STRPTR colour_labels[] = { (STRPTR)"Colour", (STRPTR)"Black and white", NULL };
static const char *pages_labels[] = { "_All", "Ran_ge" };

static struct PrintRequester {
    Object *window, *printer, *find, *copies, *pages, *range, *paper, *layout, *sides, *colour;
    Object *print, *save, *cancel, *preview, *info, *status, *fuel;
    struct Window *win;
    struct Screen *screen;
    struct DrawInfo *dri;
    struct Hook idcmp_hook, render_hook;
    OAPPrinterList printers;
    int offered[OAP_PRINTERS_MAX];             /* chooser row -> printer index */
    int offered_count;
    STRPTR printer_labels[OAP_PRINTERS_MAX + 1];
    const char *pdf;
    long pdf_bytes;
    OAPJobOptions *options;
    char status_text[256], info_text[200], cancel_text[16];
    char request[OAV_PATH_MAX];
    int busy, ticks, poll;
    struct List page_choices;
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

static const char *base_name(const char *path);
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
static const char *base_name(const char *path)
{
    const char *p = FilePart((STRPTR)path);
    return p && *p ? p : path;
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

/* ---- the gadgets' state ----------------------------------------------- */

static ULONG get(Object *o, ULONG attr)
{
    ULONG v = 0;
    GetAttr(attr, o, &v);
    return v;
}

static void set(Object *o, Tag tag, ULONG value)
{
    if (R.win)
        SetGadgetAttrs((struct Gadget *)o, R.win, NULL, tag, value, TAG_DONE);
    else
        SetAttrs(o, tag, value, TAG_DONE);
}

static void show_status(const char *text)
{
    copy(R.status_text, sizeof(R.status_text), text);
    set(R.status, GA_Text, (ULONG)R.status_text);
}

static const OAPPrinter *chosen(void)
{
    ULONG row = get(R.printer, CHOOSER_Selected);
    if ((int)row < 0 || (int)row >= R.offered_count)
        return NULL;
    return &R.printers.printer[R.offered[row]];
}

static void show_printer_info(void)
{
    const OAPPrinter *p = chosen();
    if (!p)
        copy(R.info_text, sizeof(R.info_text), "No printer chosen");
    else if (oap_printer_is_file(p))
        copy(R.info_text, sizeof(R.info_text), "Print saves the document as a PDF file on this Amiga");
    else
        snprintf(R.info_text, sizeof(R.info_text), "%s", p->note[0] ? p->note : "Verified to print PDF");
    set(R.info, GA_Text, (ULONG)R.info_text);
    set(R.print, GA_Text, (ULONG)(p && oap_printer_is_file(p) ? "_Save..." : "_Print"));
    if (!R.busy)
        set(R.save, GA_Disabled, p && oap_printer_is_file(p));
}

/* The chooser offers Save as PDF file and every printer verified to take
 * PDF; the one to select is `uri` when it is offered. */
static void fill_printers(const char *uri)
{
    int i, select = 0;
    oap_printers_load(&R.printers);
    R.offered_count = 0;
    for (i = 0; i < R.printers.count && R.offered_count < OAP_PRINTERS_MAX; i++) {
        const OAPPrinter *p = &R.printers.printer[i];
        if (!oap_printer_is_file(p) && p->pdf != OAP_PDF_YES)
            continue;
        if (uri && !strcmp(p->uri, uri))
            select = R.offered_count;
        R.offered[R.offered_count] = i;
        R.printer_labels[R.offered_count++] = (STRPTR)p->name;
    }
    R.printer_labels[R.offered_count] = NULL;
    if (R.win)
        SetGadgetAttrs((struct Gadget *)R.printer, R.win, NULL, CHOOSER_LabelArray, (ULONG)R.printer_labels,
                       CHOOSER_Selected, select, TAG_DONE);
    else
        SetAttrs(R.printer, CHOOSER_LabelArray, (ULONG)R.printer_labels, CHOOSER_Selected, select, TAG_DONE);
    show_printer_info();
}

static void read_options(OAPJobOptions *o)
{
    const OAPPrinter *p = chosen();
    STRPTR range = (STRPTR)get(R.range, STRINGA_TextVal);
    int a = 0, b = 0;
    copy(o->printer_uri, sizeof(o->printer_uri), p ? p->uri : "");
    o->copies = (int)get(R.copies, INTEGER_Number);
    if (o->copies < 1)
        o->copies = 1;
    o->paper = get(R.paper, CHOOSER_Selected) ? OAP_PAPER_LETTER : OAP_PAPER_A4;
    o->orientation = get(R.layout, CHOOSER_Selected) ? OAP_LANDSCAPE : OAP_PORTRAIT;
    o->duplex = (int)get(R.sides, CHOOSER_Selected);
    o->color = get(R.colour, CHOOSER_Selected) ? OAP_MONO : OAP_COLOR;
    o->page_start = o->page_end = 0;
    if (get(R.pages, RADIOBUTTON_Selected) && range) {
        if (sscanf((char *)range, "%d-%d", &a, &b) == 2 && a > 0 && b >= a) {
            o->page_start = a;
            o->page_end = b;
        } else if (sscanf((char *)range, "%d", &a) == 1 && a > 0)
            o->page_start = o->page_end = a;
    }
}

static void set_busy(int busy, const char *cancel_label)
{
    R.busy = busy;
    set(R.print, GA_Disabled, busy);
    set(R.save, GA_Disabled, busy);
    set(R.printer, GA_Disabled, busy);
    set(R.find, GA_Disabled, busy);
    set(R.fuel, GA_Disabled, !busy);
    copy(R.cancel_text, sizeof(R.cancel_text), cancel_label);
    set(R.cancel, GA_Text, (ULONG)R.cancel_text);
}

/* ---- the preview: the page drawn to the paper's real shape ---------------- */

static ULONG render_preview(REG(a0, struct Hook *h), REG(a2, Object *o), REG(a1, struct gpRender *msg))
{
    struct IBox *box = NULL;
    struct RastPort *rp = msg->gpr_RPort;
    UWORD *pens = R.dri ? R.dri->dri_Pens : NULL;
    long pw, ph, avail_w, avail_h, w, hgt, x, y, line;
    int landscape = R.options->orientation == OAP_LANDSCAPE;
    char caption[96];
    (void)h;
    GetAttr(SPACE_AreaBox, o, (ULONG *)&box);
    if (!box || box->Width < 20 || box->Height < 30)
        return 0;
    pw = R.options->paper == OAP_PAPER_LETTER ? 216 : 210;
    ph = R.options->paper == OAP_PAPER_LETTER ? 279 : 297;
    if (landscape) {
        long t = pw;
        pw = ph;
        ph = t;
    }
    SetAPen(rp, pens ? pens[BACKGROUNDPEN] : 0);
    RectFill(rp, box->Left, box->Top, box->Left + box->Width - 1, box->Top + box->Height - 1);
    avail_w = box->Width - 16;
    avail_h = box->Height - 16 - rp->TxHeight - 4;
    w = avail_w;
    hgt = w * ph / pw;
    if (hgt > avail_h) {
        hgt = avail_h;
        w = hgt * pw / ph;
    }
    if (w < 8 || hgt < 8)
        return 0;
    x = box->Left + (box->Width - w) / 2;
    y = box->Top + 6;
    SetAPen(rp, pens ? pens[SHADOWPEN] : 1);                    /* the shadow */
    RectFill(rp, x + 3, y + 3, x + w + 2, y + hgt + 2);
    SetAPen(rp, pens ? pens[SHINEPEN] : 2);                     /* the paper */
    RectFill(rp, x, y, x + w - 1, y + hgt - 1);
    SetAPen(rp, pens ? pens[SHADOWPEN] : 1);
    Move(rp, x, y);
    Draw(rp, x + w - 1, y);
    Draw(rp, x + w - 1, y + hgt - 1);
    Draw(rp, x, y + hgt - 1);
    Draw(rp, x, y);
    /* the page's text, sketched: a heading and lines inside the margins */
    SetAPen(rp, pens ? pens[TEXTPEN] : 1);
    RectFill(rp, x + w / 8, y + hgt / 10, x + w / 8 + w / 2, y + hgt / 10 + (hgt > 120 ? 3 : 1));
    SetAPen(rp, pens ? pens[FILLPEN] : 3);
    for (line = y + hgt / 10 + hgt / 12; line < y + hgt - hgt / 10; line += hgt > 160 ? 7 : 5) {
        long end = x + w - w / 8 - ((line / 5) % 3) * (w / 10);
        Move(rp, x + w / 8, line);
        Draw(rp, end, line);
    }
    if (R.pdf_bytes >= 10240)
        snprintf(caption, sizeof(caption), "PDF, %ld KB", R.pdf_bytes / 1024);
    else
        snprintf(caption, sizeof(caption), "PDF, %ld bytes", R.pdf_bytes < 0 ? 0L : R.pdf_bytes);
    {
        struct TextExtent te;
        ULONG n = TextFit(rp, (STRPTR)caption, strlen(caption), &te, NULL, 1, box->Width - 4, rp->TxHeight + 1);
        SetAPen(rp, pens ? pens[TEXTPEN] : 1);
        SetDrMd(rp, JAM1);
        Move(rp, box->Left + (box->Width - te.te_Width) / 2, y + hgt + 6 + rp->TxBaseline);
        Text(rp, (STRPTR)caption, n);
    }
    return 0;
}

static ULONG idcmp(REG(a0, struct Hook *h), REG(a2, Object *o), REG(a1, struct IntuiMessage *im))
{
    (void)h;
    (void)o;
    if (im->Class == IDCMP_INTUITICKS && ++R.ticks >= 5) {
        R.ticks = 0;
        R.poll = 1;
    }
    return 0;
}

static void redraw_preview(void)
{
    if (R.win)
        RefreshGList((struct Gadget *)R.preview, R.win, NULL, 1);
}

/* ---- actions ----------------------------------------------------------- */

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
        set_busy(1, "_Stop");
        set(R.fuel, FUELGAUGE_Level, 0);
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

/* The worker's progress: "Uploading 42% (...)" moves the fuel gauge. */
static void poll_worker(void)
{
    char state[32], msg[256];
    const char *pct;
    if (!R.busy || !oav_result(R.request, state, sizeof(state), msg, sizeof(msg)))
        return;
    pct = strstr(msg, "Uploading ");
    if (pct)
        set(R.fuel, FUELGAUGE_Level, (ULONG)atoi(pct + 10));
    show_status(msg);
    if (oav_result_terminal(state)) {
        int sent = !strcmp(state, "submitted") || !strcmp(state, "uncertain");
        set_busy(0, "_Close");
        if (!strcmp(state, "submitted"))
            set(R.fuel, FUELGAUGE_Level, 100);
        /* a job accepted, or perhaps accepted, is never sent a second time from here */
        set(R.print, GA_Disabled, sent);
    }
}

/* ---- the window -------------------------------------------------------- */

static Object *label(const char *text)
{
    return NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)text, TAG_DONE);
}

static Object *button(const char *text, ULONG id)
{
    return NewObject(BUTTON_GetClass(), NULL, GA_ID, id, GA_RelVerify, TRUE, GA_Text, (ULONG)text, TAG_DONE);
}

static Object *chooser(ULONG id, STRPTR *labels, ULONG active)
{
    return NewObject(CHOOSER_GetClass(), NULL, GA_ID, id, GA_RelVerify, TRUE, CHOOSER_PopUp, TRUE,
                     CHOOSER_LabelArray, (ULONG)labels, CHOOSER_Selected, active, TAG_DONE);
}

static int open_libraries(void)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    AslBase = OpenLibrary((STRPTR)"asl.library", 38);
    WindowBase = OpenLibrary((STRPTR)"window.class", 44);
    LayoutBase = OpenLibrary((STRPTR)"gadgets/layout.gadget", 44);
    ButtonBase = OpenLibrary((STRPTR)"gadgets/button.gadget", 44);
    ChooserBase = OpenLibrary((STRPTR)"gadgets/chooser.gadget", 44);
    IntegerBase = OpenLibrary((STRPTR)"gadgets/integer.gadget", 44);
    RadioButtonBase = OpenLibrary((STRPTR)"gadgets/radiobutton.gadget", 44);
    StringBase = OpenLibrary((STRPTR)"gadgets/string.gadget", 44);
    FuelGaugeBase = OpenLibrary((STRPTR)"gadgets/fuelgauge.gadget", 44);
    SpaceBase = OpenLibrary((STRPTR)"gadgets/space.gadget", 44);
    LabelBase = OpenLibrary((STRPTR)"images/label.image", 44);
    return IntuitionBase && GfxBase && AslBase && WindowBase && LayoutBase && ButtonBase && ChooserBase && IntegerBase &&
           RadioButtonBase && StringBase && FuelGaugeBase && SpaceBase && LabelBase;
}

static void close_libraries(void)
{
    struct Library **libs[] = { &LabelBase, &SpaceBase, &FuelGaugeBase, &StringBase, &RadioButtonBase, &IntegerBase,
                                &ChooserBase, &ButtonBase, &LayoutBase, &WindowBase, &AslBase };
    size_t i;
    for (i = 0; i < sizeof(libs) / sizeof(libs[0]); i++)
        if (*libs[i]) {
            CloseLibrary(*libs[i]);
            *libs[i] = NULL;
        }
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    GfxBase = NULL;
    IntuitionBase = NULL;
}

static Object *build_window(const char *title)
{
    Object *settings, *buttons;
    R.printer = chooser(G_PRINTER, R.printer_labels, 0);
    R.find = button("_Find printers...", G_FIND);
    R.info = NewObject(BUTTON_GetClass(), NULL, GA_ID, G_INFO, GA_ReadOnly, TRUE, BUTTON_BevelStyle, BVS_NONE,
                       BUTTON_Justification, BCJ_LEFT, GA_Text, (ULONG)"", TAG_DONE);
    R.copies = NewObject(INTEGER_GetClass(), NULL, GA_ID, G_COPIES, GA_RelVerify, TRUE, INTEGER_Number, R.options->copies,
                         INTEGER_Minimum, 1, INTEGER_Maximum, 99, INTEGER_MaxChars, 2, INTEGER_Arrows, TRUE, TAG_DONE);
    {
        size_t i;
        NewList(&R.page_choices);
        for (i = 0; i < sizeof(pages_labels) / sizeof(pages_labels[0]); i++) {
            struct Node *n = AllocRadioButtonNode(1, RBNA_Label, (ULONG)pages_labels[i], TAG_DONE);
            if (n)
                AddTail(&R.page_choices, n);
        }
    }
    R.pages = NewObject(RADIOBUTTON_GetClass(), NULL, GA_ID, G_PAGES, GA_RelVerify, TRUE, RADIOBUTTON_Labels,
                        (ULONG)&R.page_choices, RADIOBUTTON_Selected, 0, TAG_DONE);
    R.range = NewObject(STRING_GetClass(), NULL, GA_ID, G_RANGE, GA_RelVerify, TRUE, GA_Disabled, TRUE,
                        STRINGA_MaxChars, 15, STRINGA_TextVal, (ULONG)"1-1", TAG_DONE);
    R.paper = chooser(G_PAPER, paper_labels, R.options->paper == OAP_PAPER_LETTER);
    R.layout = chooser(G_LAYOUT, layout_labels, R.options->orientation == OAP_LANDSCAPE);
    R.sides = chooser(G_SIDES, sides_labels, (ULONG)R.options->duplex);
    R.colour = chooser(G_COLOUR, colour_labels, R.options->color ? 0 : 1);
    R.preview = NewObject(SPACE_GetClass(), NULL, GA_ID, G_PREVIEW, SPACE_MinWidth, 150, SPACE_MinHeight, 180,
                          SPACE_RenderHook, (ULONG)&R.render_hook, TAG_DONE);
    R.status = NewObject(BUTTON_GetClass(), NULL, GA_ID, G_STATUS, GA_ReadOnly, TRUE, BUTTON_BevelStyle, BVS_THIN,
                         BUTTON_Justification, BCJ_LEFT, GA_Text, (ULONG)R.status_text, TAG_DONE);
    R.fuel = NewObject(FUELGAUGE_GetClass(), NULL, GA_ID, G_FUEL, GA_Disabled, TRUE, FUELGAUGE_Min, 0, FUELGAUGE_Max, 100,
                       FUELGAUGE_Level, 0, FUELGAUGE_Percent, TRUE, FUELGAUGE_Ticks, 0, TAG_DONE);
    R.print = button("_Print", G_PRINT);
    R.save = button("Save as P_DF...", G_SAVE);
    copy(R.cancel_text, sizeof(R.cancel_text), "_Cancel");
    R.cancel = button(R.cancel_text, G_CANCEL);

    settings = NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceInner, TRUE,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)R.printer,
            LAYOUT_AddChild, (ULONG)R.find, CHILD_WeightedWidth, 0, TAG_DONE),
        CHILD_Label, (ULONG)label("P_rinter"), CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)R.info, CHILD_Label, (ULONG)label(" "), CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_BevelStyle, BVS_GROUP, LAYOUT_Label, (ULONG)"Copies and pages", LAYOUT_SpaceInner, TRUE,
            LAYOUT_AddChild, (ULONG)R.copies, CHILD_Label, (ULONG)label("C_opies"), CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, (ULONG)R.pages, CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)R.range, TAG_DONE),
            CHILD_Label, (ULONG)label("Pages"), TAG_DONE), CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_BevelStyle, BVS_GROUP, LAYOUT_Label, (ULONG)"Paper", LAYOUT_SpaceInner, TRUE,
            LAYOUT_AddChild, (ULONG)R.paper, CHILD_Label, (ULONG)label("Si_ze"),
            LAYOUT_AddChild, (ULONG)R.layout, CHILD_Label, (ULONG)label("_Layout"),
            LAYOUT_AddChild, (ULONG)R.sides, CHILD_Label, (ULONG)label("Si_des"),
            LAYOUT_AddChild, (ULONG)R.colour, CHILD_Label, (ULONG)label("Colo_ur"), TAG_DONE), CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)NewObject(SPACE_GetClass(), NULL, TAG_DONE),
        TAG_DONE);

    buttons = NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ, LAYOUT_EvenSize, TRUE,
        LAYOUT_AddChild, (ULONG)R.print,
        LAYOUT_AddChild, (ULONG)R.save,
        LAYOUT_AddChild, (ULONG)R.cancel, TAG_DONE);

    return NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)title, WA_PubScreen, (ULONG)R.screen, WA_Activate, TRUE, WA_DragBar, TRUE,
        WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE, WA_IDCMP, IDCMP_INTUITICKS | IDCMP_VANILLAKEY,
        WINDOW_Position, WPOS_CENTERSCREEN, WINDOW_IDCMPHook, (ULONG)&R.idcmp_hook, WINDOW_IDCMPHookBits, IDCMP_INTUITICKS,
        WINDOW_Layout, (ULONG)NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter, TRUE, LAYOUT_DeferLayout, TRUE,
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                    LAYOUT_BevelStyle, BVS_GROUP, LAYOUT_Label, (ULONG)"Preview",
                    LAYOUT_AddChild, (ULONG)R.preview, TAG_DONE), CHILD_WeightedWidth, 40,
                LAYOUT_AddChild, (ULONG)settings, CHILD_WeightedWidth, 60, TAG_DONE),
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, (ULONG)R.status,
                LAYOUT_AddChild, (ULONG)R.fuel, CHILD_WeightedWidth, 30, TAG_DONE), CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)buttons, CHILD_WeightedHeight, 0, TAG_DONE),
        TAG_DONE);
}

/* Keys: each control's underlined letter; Return prints, Esc cancels. */
static int key(UWORD code, int *done)
{
    if (code == '\r') {
        if (!R.busy && !get(R.print, GA_Disabled))
            print_now();
        return 1;
    }
    switch (code | 0x20) {
    case 'p':
        if (!R.busy && !get(R.print, GA_Disabled))
            print_now();
        return 1;
    case 'd':
        if (!R.busy)
            save_pdf();
        return 1;
    case 'f':
        if (!R.busy)
            find_printers();
        return 1;
    case 'c':
        *done = !R.busy;
        if (R.busy) {
            oav_cancel(R.request);
            show_status("Stopping the upload; waiting for the worker's answer. Don't send it again yet.");
        }
        return 1;
    }
    if (code == 27) {                          /* Esc */
        *done = !R.busy;
        if (R.busy)
            oav_cancel(R.request);
        return 1;
    }
    return 0;
}

int oap_run_print_dialog(const char *pdf, OAPJobOptions *o)
{
    OAPSelectionListener selection;
    char title[160], picked[OAP_SELECTION_URI_MAX], start[OAP_SELECTION_URI_MAX];
    ULONG sigs, result;
    UWORD code;
    int done = 0, ok = 0;

    memset(&R, 0, sizeof(R));
    memset(&selection, 0, sizeof(selection));
    R.pdf = pdf;
    R.options = o;
    R.pdf_bytes = file_size(pdf);
    load_settings(o);
    copy(R.status_text, sizeof(R.status_text), R.pdf_bytes >= 0 ? "Ready" : "The document to print can't be read");
    R.idcmp_hook.h_Entry = (ULONG (*)())idcmp;
    R.render_hook.h_Entry = (ULONG (*)())render_preview;
    if (!open_libraries())
        goto out;
    R.screen = LockPubScreen(NULL);
    if (!R.screen)
        goto out;
    R.dri = GetScreenDrawInfo(R.screen);
    snprintf(title, sizeof(title), "Print: %s", job_title(pdf));

    /* start on the printer the job asked for, else the default */
    copy(start, sizeof(start), o->printer_uri);
    if (!start[0] || strstr(start, "printer.local"))
        GetVar((STRPTR)"OpenAmigaPrint/PrinterURI", (STRPTR)start, sizeof(start), GVF_GLOBAL_ONLY);
    oap_printers_load(&R.printers);
    R.printer_labels[0] = NULL;
    R.window = build_window(title);
    if (!R.window)
        goto out;
    fill_printers(start);
    R.win = RA_OpenWindow(R.window);
    if (!R.win)
        goto out;
    show_printer_info();
    oap_selection_open(&selection);
    ok = 1;

    while (!done) {
        GetAttr(WINDOW_SigMask, R.window, &sigs);
        Wait(sigs | oap_selection_mask(&selection) | SIGBREAKF_CTRL_C);
        if (oap_selection_receive(&selection, picked, sizeof(picked)) && !R.busy) {
            char text[200];
            fill_printers(picked);
            snprintf(text, sizeof(text), "Printer chosen: %s", chosen() ? chosen()->name : picked);
            show_status(text);
        }
        while ((result = RA_HandleInput(R.window, &code)) != WMHI_LASTMSG) {
            switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                if (R.busy)
                    oav_cancel(R.request);
                done = 1;
                break;
            case WMHI_VANILLAKEY:
                key(code, &done);
                break;
            case WMHI_GADGETUP:
                switch (result & WMHI_GADGETMASK) {
                case G_PRINTER:
                    show_printer_info();
                    break;
                case G_FIND:
                    find_printers();
                    break;
                case G_PAGES:
                    set(R.range, GA_Disabled, get(R.pages, RADIOBUTTON_Selected) == 0);
                    break;
                case G_PAPER: case G_LAYOUT:
                    read_options(o);
                    redraw_preview();
                    break;
                case G_PRINT:
                    print_now();
                    break;
                case G_SAVE:
                    save_pdf();
                    break;
                case G_CANCEL:
                    if (R.busy) {
                        oav_cancel(R.request);
                        show_status("Stopping the upload; waiting for the worker's answer. Don't send it again yet.");
                    } else
                        done = 1;
                    break;
                }
                break;
            }
        }
        if (R.poll) {
            R.poll = 0;
            poll_worker();
        }
    }

out:
    oap_selection_close(&selection);
    if (R.window)
        DisposeObject(R.window);
    if (RadioButtonBase && R.page_choices.lh_Head) {
        struct Node *n;
        while ((n = RemHead(&R.page_choices)))
            FreeRadioButtonNode(n);
    }
    if (R.dri)
        FreeScreenDrawInfo(R.screen, R.dri);
    if (R.screen)
        UnlockPubScreen(NULL, R.screen);
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
