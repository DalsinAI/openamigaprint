/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* OpenView: open, look, print. GadTools owns the window (We, 4 October
 * 2026: OS 3.x applications use GadTools or MUI, not ReAction);
 * datatypes.library owns what is shown in it.
 *   - Page setup is three labelled choices beside the picture; they apply to
 *     pictures, the only thing laid out on paper here.
 *   - Print... hands over to the Print requester: a PDF as it is, a picture
 *     as a PDF the worker makes first (C:OAVWorker).
 *   - Animations and sounds bring their datatype's own player bar.
 *   - A file dropped on the window opens; the ARexx port OPENVIEW takes
 *     the same commands as before.
 *   - OpenView 0.5 stands in for MultiView (OpenUp installs it as
 *     SYS:Utilities/MultiView): MultiView 47's command line (every keyword
 *     and switch is read; the ones OpenView can't honour are ignored), the
 *     Workbench tool types of the project's icon, and ARexx on the port
 *     MULTIVIEW.n (or PORTNAME) with MultiView's commands as well. What it
 *     shows is whatever a datatype reads: no kind of file is named here. */
#include "oav_core.h"
#include "oav_jobs.h"
#include "oap_queue.h"
#include "oap.h"
#include "oap_gt.h"
#include "oap_selection.h"
#include "oap_printers.h"
#include "oap_stack.h"
#include "oap_str.h"
#include <exec/types.h>
#include <exec/lists.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <intuition/intuitionbase.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <graphics/gfxbase.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <datatypes/animationclass.h>
#include <libraries/asl.h>
#include <libraries/amigaguide.h>
#include <libraries/gadtools.h>
#include <rexx/storage.h>
#include <rexx/errors.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/datatypes.h>
#include <proto/asl.h>
#include <proto/gadtools.h>
#include <proto/wb.h>
#include <proto/rexxsyslib.h>
#include <proto/icon.h>
#include <workbench/icon.h>
#include <clib/alib_protos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *UtilityBase, *DataTypesBase, *AslBase, *GadToolsBase, *WorkbenchBase, *IconBase;
struct RxsLib *RexxSysBase;
static const char oap_version[] __attribute__((used)) = "$VER: OpenView 0.5 (9.10.2026)";

#define MAX_JOBS 128
#define REXX_NAME "OPENVIEW"
/* MultiView 47's template, word for word (MultiView ? says it too) */
#define MV_TEMPLATE "FILE,CLIPBOARD/S,CLIPUNIT/K/N,SCREEN/S,PUBSCREEN/K,REQUESTER/S,BOOKMARK/S,FONTNAME/K,FONTSIZE/K/N," \
                    "BACKDROP/S,WINDOW/S,PORTNAME/K,IMMEDIATE/S,REPEAT/S,PRTUNIT/K/N,WINDOWLEFT/K/N,WINDOWTOP/K/N," \
                    "WINDOWWIDTH/K/N,WINDOWHEIGHT/K/N,AUTORESIZE/S"

enum {
    B_OPEN = 1, B_PAGE, B_FIT, B_ONE, B_MINUS, B_PLUS, B_PLAY, B_PAUSE, B_STOP, B_PREV, B_NEXT,
    B_PAPER, B_ORIENT, B_SCALE, B_EXPORT, B_PRINT, B_REFRESH, B_CANCEL, B_INFO, B_VSCROLL, B_HSCROLL,
    B_STATUS, B_QUIT, B_PRINTDLG, B_VIEW, B_PRINTERS, B_FIND, B_FINDNEXT,
    RX_SET = 200, RX_VERSION, RX_HELP, RX_JOB, RX_JOBS,
    /* MultiView's ARexx commands */
    RX_RELOAD, RX_SAVEAS, RX_ABOUT, RX_COPY, RX_PASTE, RX_CLEARSEL, RX_GETTRIG, RX_DOTRIG, RX_SCREEN, RX_PUBSCREEN,
    RX_GETDIR, RX_GETFILE, RX_GETOBJ, RX_MINSIZE, RX_NORMSIZE, RX_MAXSIZE, RX_WTOFRONT, RX_WTOBACK, RX_STOFRONT,
    RX_STOBACK, RX_ACTIVATE, RX_BEEP
};

/* MultiView's command line (or the project icon's tool types), as read. Numbers are -1 when not given. */
typedef struct Options {
    char file[OAV_PATH_MAX], pubscreen[64], portname[64], fontname[64];
    int clipboard, requester, window, immediate, repeat;
    long fontsize, left, top, width, height;
} Options;

typedef struct QueueRow { char name[128], path[OAV_PATH_MAX], state[32]; } QueueRow;

static STRPTR view_labels[] = { (STRPTR)"As the page", (STRPTR)"As the file", NULL };
static STRPTR paper_labels[] = { (STRPTR)"A4", (STRPTR)"Letter", NULL };
static STRPTR orient_labels[] = { (STRPTR)"Portrait", (STRPTR)"Landscape", NULL };
static STRPTR scale_labels[] = { (STRPTR)"Fit to page", (STRPTR)"Fill and crop", NULL };

static struct NewMenu menus[] = {
    { NM_TITLE, (STRPTR)"Project", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Open...", (STRPTR)"O", 0, 0, (APTR)B_OPEN },
    { NM_ITEM, (STRPTR)"Save as PDF...", (STRPTR)"S", 0, 0, (APTR)B_EXPORT },
    { NM_ITEM, (STRPTR)"About this file", (STRPTR)"I", 0, 0, (APTR)B_INFO },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)B_QUIT },
    { NM_TITLE, (STRPTR)"View", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Fit in window", (STRPTR)"F", 0, 0, (APTR)B_FIT },
    { NM_ITEM, (STRPTR)"Actual size", (STRPTR)"1", 0, 0, (APTR)B_ONE },
    { NM_ITEM, (STRPTR)"Zoom in", (STRPTR)"+", 0, 0, (APTR)B_PLUS },
    { NM_ITEM, (STRPTR)"Zoom out", (STRPTR)"-", 0, 0, (APTR)B_MINUS },
    { NM_ITEM, (STRPTR)"As the page / as the file", (STRPTR)"L", 0, 0, (APTR)B_PAGE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Find...", (STRPTR)"G", 0, 0, (APTR)B_FIND },
    { NM_ITEM, (STRPTR)"Find next", (STRPTR)"N", 0, 0, (APTR)B_FINDNEXT },
    { NM_TITLE, (STRPTR)"Print", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Print...", (STRPTR)"P", 0, 0, (APTR)B_PRINTDLG },
    { NM_ITEM, (STRPTR)"Printers and queue...", (STRPTR)"R", 0, 0, (APTR)B_PRINTERS },
    { NM_ITEM, (STRPTR)"Stop preparing", (STRPTR)".", 0, 0, (APTR)B_CANCEL },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

/* Where everything goes, from the window's size and the font. */
typedef struct Geo {
    int view_x, view_y, view_w, view_h;     /* the frame round the picture */
    int side_x, side_w;                     /* page setup and the print buttons */
    int y_setup, setup_h, y_status;
    int scroll_w, scroll_h;
} Geo;

static struct App {
    OAPGT g;
    Geo geo;
    struct Window *win;
    struct Gadget *glist;
    struct Menu *menu;
    struct AppWindow *appwin;
    struct MsgPort *appport, *rexxport, *mvport;
    Options opt;
    struct TextAttr fontattr;
    char fontbuf[80], mvname[64], fullpath[OAV_PATH_MAX], infoline[256];
    LONG normal_w, normal_h;
    Object *dto;
    struct IBox area;                       /* where the datatype object may draw */
    char title[160];
    int print_after;                        /* open the Print requester when the queued PDF is ready */
    QueueRow jobs[MAX_JOBS];
    OAVLayout settings;
    OAVPlacement placement;
    struct IBox paperbox;
    ULONG group, natural_w, natural_h, whitepen;
    OAPSelectionListener selection;
    char printer_saved[384];
    char path[OAV_PATH_MAX], lastreq[OAV_PATH_MAX], message[256], resultbuf[2048];
    int running, refresh, page, zoom, job_active, qcount, have_white, ticks, attached, setup_on;
    /* Find: the file's text, read when first searched */
    char *find_buf, find_what[80], find_node[64], open_node[64];   /* open_node: a guide opens at this node */
    long find_len, find_pos, find_top;      /* find_top: a line still to show once a guide node is laid out */
    int find_guide, find_wait;
} A;

#define copystr oap_copy

static int is_pdf(const char *path)
{
    const char *ext = strrchr(path, '.');
    return ext && !strcasecmp(ext, ".pdf");
}

/* ---- gadgets ------------------------------------------------------------- */

/* A gadget of ours, only while the list is in the window (GadTools may only
 * change gadgets that are in their window). */
static struct Gadget *gad(UWORD id)
{
    return A.attached ? oap_gt_find(A.glist, id) : NULL;
}

static void set_gadget(UWORD id, Tag tag, ULONG value)
{
    struct Gadget *g = gad(id);
    if (g)
        GT_SetGadgetAttrs(g, A.win, NULL, tag, value, TAG_DONE);
}

static void status(const char *s)
{
    copystr(A.message, sizeof(A.message), s);
    set_gadget(B_STATUS, GTTX_Text, (ULONG)A.message);
}

static void set_title(const char *path)
{
    snprintf(A.title, sizeof(A.title), "OpenView: %.120s", FilePart((STRPTR)path));
    if (A.win)
        SetWindowTitles(A.win, (UBYTE *)A.title, (UBYTE *)~0);
}

/* Page setup applies to pictures, the only thing laid out on paper here. */
static void page_setup_applies(int yes)
{
    A.setup_on = yes;
    set_gadget(B_PAPER, GA_Disabled, !yes);
    set_gadget(B_ORIENT, GA_Disabled, !yes);
    set_gadget(B_SCALE, GA_Disabled, !yes);
    set_gadget(B_VIEW, GA_Disabled, !yes);
}

static void show_settings(void)
{
    set_gadget(B_PAPER, GTCY_Active, A.settings.paper ? 1 : 0);
    set_gadget(B_ORIENT, GTCY_Active, A.settings.landscape ? 1 : 0);
    set_gadget(B_SCALE, GTCY_Active, A.settings.scale ? 1 : 0);
    set_gadget(B_VIEW, GTCY_Active, A.page ? 0 : 1);
}

static int trigger_supported(ULONG id)
{
    struct DTMethod *m;
    int i;
    if (!A.dto)
        return 0;
    m = (struct DTMethod *)GetDTTriggerMethods(A.dto);
    if (!m)
        return 0;
    for (i = 0; i < 128 && m[i].dtm_Label; i++)
        if ((m[i].dtm_Method & STMF_METHOD_MASK) == id)
            return 1;
    return 0;
}

/* The scrollers follow what the datatype shows. */
static void scroll_info(void)
{
    ULONG th = 0, tv = 0, vh = 1, vv = 1, nh = 1, nv = 1;
    struct Gadget *v = gad(B_VSCROLL), *h = gad(B_HSCROLL);
    if (!A.dto || !A.win)
        return;
    GetDTAttrs(A.dto, DTA_TopHoriz, (ULONG)&th, DTA_TopVert, (ULONG)&tv, DTA_VisibleHoriz, (ULONG)&vh, DTA_VisibleVert, (ULONG)&vv,
               DTA_TotalHoriz, (ULONG)&nh, DTA_TotalVert, (ULONG)&nv, TAG_DONE);
    if (v)
        GT_SetGadgetAttrs(v, A.win, NULL, GTSC_Top, tv, GTSC_Visible, vv, GTSC_Total, nv, TAG_DONE);
    if (h)
        GT_SetGadgetAttrs(h, A.win, NULL, GTSC_Top, th, GTSC_Visible, vh, GTSC_Total, nh, TAG_DONE);
}

/* ---- the view: the paper behind a picture, or why nothing shows ---------- */

static void draw_view(void)
{
    struct RastPort *rp;
    struct IBox *b = &A.area;
    if (!A.win || b->Width < 1 || b->Height < 1)
        return;
    rp = A.win->RPort;
    SetAPen(rp, oap_gt_pen(&A.g, BACKGROUNDPEN));
    RectFill(rp, b->Left, b->Top, b->Left + b->Width - 1, b->Top + b->Height - 1);
    if (A.page && A.group == GID_PICTURE && A.paperbox.Width > 0) {
        SetAPen(rp, oap_gt_pen(&A.g, SHADOWPEN));
        RectFill(rp, A.paperbox.Left - 1, A.paperbox.Top - 1, A.paperbox.Left + A.paperbox.Width, A.paperbox.Top + A.paperbox.Height);
        SetAPen(rp, A.whitepen);
        RectFill(rp, A.paperbox.Left, A.paperbox.Top, A.paperbox.Left + A.paperbox.Width - 1, A.paperbox.Top + A.paperbox.Height - 1);
    }
    if (!A.dto && A.path[0] && is_pdf(A.path)) {     /* a PDF with nothing to show it: say so where the page would be */
        static const char *lines[2] = { "This PDF can't be shown here.", "Print... still prints it." };
        int i;
        for (i = 0; i < 2; i++) {
            int tw = oap_gt_text_w(&A.g, lines[i]);
            oap_gt_text(&A.g, rp, b->Left + (b->Width - tw) / 2, b->Top + b->Height / 2 + (i * 2 - 1) * A.g.line_h - A.g.fh / 2,
                        lines[i], TEXTPEN, b->Width);
        }
    }
    A.refresh = 1;
}

static void find_forget(void)
{
    free(A.find_buf);
    A.find_buf = NULL;
    A.find_len = A.find_pos = 0;
    A.find_top = -1;
    A.find_node[0] = 0;
}

static void close_content(void)
{
    if (A.dto) {
        if (A.win)
            RemoveDTObject(A.win, A.dto);
        DisposeDTObject(A.dto);
        A.dto = NULL;
    }
    A.group = 0;
}

static void pdf_not_shown(const char *path)
{
    char msg[256];
    close_content();
    find_forget();
    A.paperbox.Width = 0;
    page_setup_applies(0);
    copystr(A.path, sizeof(A.path), path);
    set_title(path);
    snprintf(msg, sizeof(msg), "%.40s: can't be shown (no PDF datatype). Print... still prints it.", FilePart((STRPTR)path));
    status(msg);
    draw_view();
}

static int do_trigger(ULONG id);

/* The file's size (0 for a drawer, which a datatype may show too), its full name, and -1 when it can't be examined. */
static long file_facts(const char *path, int *is_dir, char *full, size_t fullsize)
{
    struct FileInfoBlock *fib;
    BPTR lock;
    long size = -1;
    *is_dir = 0;
    full[0] = 0;
    lock = Lock((STRPTR)path, ACCESS_READ);
    if (!lock)
        return -1;
    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (fib) {
        if (Examine(lock, fib)) {
            *is_dir = fib->fib_DirEntryType > 0;
            size = *is_dir ? 0 : (long)fib->fib_Size;
        }
        FreeDosObject(DOS_FIB, fib);
    }
    if (!NameFromLock(lock, (STRPTR)full, (LONG)fullsize))
        copystr(full, fullsize, path);
    UnLock(lock);
    return size;
}

static int load_file(const char *path)
{
    Object *dto;
    struct DataType *dt = NULL;
    struct BitMapHeader *bmh = NULL;
    struct IBox *area = &A.area;
    ULONG group = 0, w = 1, h = 1, sw, sh;
    LONG left, top, dw, dh, cropx = 0, cropy = 0;
    struct pdtScale scale;
    struct gpLayout prep;
    struct FrameInfo fi;
    struct dtFrameBox frame;
    char msg[256];
    long bytes;
    int is_dir, fresh = 0;
    char full[OAV_PATH_MAX];

    if (!oav_safe_field(path)) {
        status("Invalid or overlong file path");
        return 0;
    }
    bytes = file_facts(path, &is_dir, full, sizeof(full));
    if (bytes < 0) {
        status("Cannot open source file");
        return 0;
    }
    if (bytes > (long)OAV_FILE_LIMIT) {
        status("Source exceeds the 64 MiB viewer file limit");
        return 0;
    }
    status("Loading datatype...");
    dto = NewDTObject((APTR)path, A.open_node[0] ? DTA_NodeName : TAG_IGNORE, (ULONG)A.open_node,
                      A.fontattr.ta_Name ? DTA_TextAttr : TAG_IGNORE, (ULONG)&A.fontattr,
                      DTA_Repeat, (ULONG)(A.opt.repeat ? TRUE : FALSE),
                      DTA_SourceType, DTST_FILE, ICA_TARGET, ICTARGET_IDCMP, PDTA_DestMode, PMODE_V43,
                      PDTA_Screen, (ULONG)A.g.screen, PDTA_Remap, TRUE, AGA_Secure, TRUE, DTA_ControlPanel, TRUE, GA_ID, 1000, TAG_DONE);
    if (!dto) {
        LONG err = IoErr();
        const char *name = (const char *)FilePart((STRPTR)path);
        if (is_pdf(path)) {
            pdf_not_shown(path);
            return 1;
        }
        if (err == ERROR_OBJECT_WRONG_TYPE || err == 2000) {
            const char *note = oav_format_note(path);
            snprintf(msg, sizeof(msg), "%.50s: no datatype on this Amiga opens this kind of file. %s", name,
                     strncmp(note, "A compatible", 12) ? note : "");
        } else
            snprintf(msg, sizeof(msg), "%.60s couldn't be opened (DOS error %ld)", name, (long)err);
        status(msg);
        return 0;
    }
    memset(&fi, 0, sizeof(fi));
    memset(&frame, 0, sizeof(frame));
    frame.MethodID = DTM_FRAMEBOX;
    frame.dtf_ContentsInfo = &fi;
    frame.dtf_FrameInfo = &fi;
    frame.dtf_SizeFrameInfo = sizeof(fi);
    DoDTMethodA(dto, NULL, NULL, (Msg)&frame);
    GetDTAttrs(dto, DTA_DataType, (ULONG)&dt, DTA_NominalHoriz, (ULONG)&w, DTA_NominalVert, (ULONG)&h, TAG_DONE);
    if (dt && dt->dtn_Header)
        group = dt->dtn_Header->dth_GroupID;
    if (is_pdf(path) && group != GID_DOCUMENT && group != GID_PICTURE) {   /* e.g. ascii.datatype showing PDF source */
        DisposeDTObject(dto);
        pdf_not_shown(path);
        return 1;
    }
    if (group == GID_PICTURE) {
        GetDTAttrs(dto, PDTA_BitMapHeader, (ULONG)&bmh, TAG_DONE);
        if (bmh) {
            w = bmh->bmh_Width;
            h = bmh->bmh_Height;
        }
    }
    if (!w) w = 1;
    if (!h) h = 1;
    if (group == GID_PICTURE && w > OAV_PIXEL_LIMIT / h) {
        DisposeDTObject(dto);
        status("Picture exceeds 16 megapixel preview limit");
        return 0;
    }
    if (area->Width < 8 || area->Height < 8) {
        DisposeDTObject(dto);
        status("The window is too small to show it");
        return 0;
    }
    left = area->Left + 2;
    top = area->Top + 2;
    dw = area->Width - 4;
    dh = area->Height - 4;
    sw = w;
    sh = h;
    A.paperbox.Width = 0;
    if (group == GID_PICTURE) {
        if (A.page && oav_place(&A.settings, w, h, &A.placement)) {
            if ((long)(dw - 12) * A.placement.page_h <= (long)(dh - 12) * A.placement.page_w) {
                A.paperbox.Width = (WORD)(dw - 12);
                A.paperbox.Height = (WORD)oav_scale(A.placement.page_h, dw - 12, A.placement.page_w);
            } else {
                A.paperbox.Height = (WORD)(dh - 12);
                A.paperbox.Width = (WORD)oav_scale(A.placement.page_w, dh - 12, A.placement.page_h);
            }
            A.paperbox.Left = (WORD)(left + (dw - A.paperbox.Width) / 2);
            A.paperbox.Top = (WORD)(top + (dh - A.paperbox.Height) / 2);
            sw = (ULONG)oav_scale(A.placement.w, A.paperbox.Width, A.placement.page_w);
            sh = (ULONG)oav_scale(A.placement.h, A.paperbox.Width, A.placement.page_w);
            left = A.paperbox.Left + oav_scale(A.placement.x, A.paperbox.Width, A.placement.page_w);
            top = A.paperbox.Top + oav_scale(A.placement.page_h - A.placement.y - A.placement.h, A.paperbox.Width, A.placement.page_w);
            {
                LONG cx = A.paperbox.Left + oav_scale(A.placement.clip_x, A.paperbox.Width, A.placement.page_w);
                LONG cy = A.paperbox.Top + oav_scale(A.placement.clip_y, A.paperbox.Width, A.placement.page_w);
                LONG cw = oav_scale(A.placement.clip_w, A.paperbox.Width, A.placement.page_w);
                LONG ch = oav_scale(A.placement.clip_h, A.paperbox.Width, A.placement.page_w);
                cropx = left < cx ? cx - left : 0;
                cropy = top < cy ? cy - top : 0;
                if (left < cx) left = cx;
                if (top < cy) top = cy;
                dw = (LONG)sw - cropx;
                dh = (LONG)sh - cropy;
                if (left + dw > cx + cw) dw = cx + cw - left;
                if (top + dh > cy + ch) dh = cy + ch - top;
            }
        } else {
            if (A.zoom) {
                sw = w * (ULONG)A.zoom / 100;
                sh = h * (ULONG)A.zoom / 100;
            } else if ((ULONG)dw * h <= (ULONG)dh * w) {
                sw = (ULONG)dw;
                sh = (h * (ULONG)dw + w / 2) / w;
            } else {
                sh = (ULONG)dh;
                sw = (w * (ULONG)dh + h / 2) / h;
            }
        }
        if (sw < 1) sw = 1;
        if (sh < 1) sh = 1;
        if (sw <= 8192 && sh <= 8192 && (sw != w || sh != h)) {
            scale.MethodID = PDTM_SCALE;
            scale.ps_NewWidth = sw;
            scale.ps_NewHeight = sh;
            scale.ps_Flags = 0;
            if (!DoMethodA(dto, (Msg)&scale)) {
                A.paperbox.Width = 0;
                left = area->Left + 2;
                top = area->Top + 2;
                dw = area->Width - 4;
                dh = area->Height - 4;
                cropx = cropy = 0;
            }
        }
    }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    SetDTAttrs(dto, NULL, NULL, GA_Left, left, GA_Top, top, GA_Width, dw, GA_Height, dh, DTA_TopHoriz, cropx, DTA_TopVert, cropy, TAG_DONE);
    if (group == GID_PICTURE) {
        memset(&prep, 0, sizeof(prep));
        prep.MethodID = DTM_PROCLAYOUT;
        prep.gpl_Initial = TRUE;
        if (!DoDTMethodA(dto, NULL, NULL, (Msg)&prep)) {
            DisposeDTObject(dto);
            status("Picture layout failed before display");
            return 0;
        }
    }
    close_content();
    if (strcmp(A.path, path)) {
        find_forget();                         /* another file: its text is read again when searched */
        fresh = 1;
    }
    A.dto = dto;
    A.group = group;
    A.natural_w = w;
    A.natural_h = h;
    copystr(A.path, sizeof(A.path), path);
    copystr(A.fullpath, sizeof(A.fullpath), full);
    draw_view();
    if (AddDTObject(A.win, NULL, dto, -1) < 0) {
        DisposeDTObject(dto);
        A.dto = NULL;
        status("Datatype could not attach to the preview window");
        return 0;
    }
    set_title(A.path);
    page_setup_applies(group == GID_PICTURE);
    if (group == GID_PICTURE || group == GID_ANIMATION)
        snprintf(msg, sizeof(msg), "%.60s \xb7 %s, %lu \xd7 %lu%s", FilePart((STRPTR)path),
                 dt && dt->dtn_Header ? (char *)dt->dtn_Header->dth_Name : "Datatype", (unsigned long)w, (unsigned long)h,
                 group == GID_ANIMATION ? " \xb7 animation" : "");
    else
        snprintf(msg, sizeof(msg), "%.60s \xb7 %s", FilePart((STRPTR)path), dt && dt->dtn_Header ? (char *)dt->dtn_Header->dth_Name : "Datatype");
    status(msg);
    copystr(A.infoline, sizeof(A.infoline), msg);
    RefreshDTObjectA(A.dto, A.win, NULL, NULL);
    scroll_info();
    if (fresh && A.opt.immediate && trigger_supported(STM_PLAY))      /* IMMEDIATE: an animation or a sound starts at once */
        do_trigger(STM_PLAY);
    return 1;
}

static void reload(void)
{
    char path[OAV_PATH_MAX];
    if (!A.path[0])
        return;
    copystr(path, sizeof(path), A.path);
    load_file(path);
}

static int choose_file(char *path, int save)
{
    struct FileRequester *r = AllocAslRequestTags(ASL_FileRequest, ASLFR_Window, (ULONG)A.win,
        ASLFR_TitleText, (ULONG)(save ? "Save PDF (new filename)" : "Open document, picture or animation"), ASLFR_DoSaveMode, save,
        ASLFR_InitialDrawer, (ULONG)"Work:", ASLFR_InitialFile, (ULONG)(save ? "Picture.pdf" : ""), ASLFR_SleepWindow, TRUE, TAG_DONE);
    int ok = 0;
    if (r) {
        if (AslRequestTags(r, TAG_DONE)) {
            copystr(path, OAV_PATH_MAX, (char *)r->fr_Drawer);
            if (AddPart((STRPTR)path, r->fr_File, OAV_PATH_MAX))
                ok = 1;
        }
        FreeAslRequest(r);
    }
    return ok;
}

/* ---- jobs ------------------------------------------------------------------ */

static int qcmp(const void *a, const void *b)
{
    return strcmp(((const QueueRow *)a)->name, ((const QueueRow *)b)->name);
}

/* The queue, for ARexx's JOBS (the Printers and Queue window shows it). */
static void scan_queue(void)
{
    DIR *d;
    struct dirent *e;
    A.qcount = 0;
    d = opendir(OAP_QUEUE_DIR);
    if (!d)
        return;
    while ((e = readdir(d)) && A.qcount < MAX_JOBS) {
        size_t n = strlen(e->d_name);
        FILE *f;
        char mp[OAV_PATH_MAX], line[256];
        QueueRow *r;
        if (n < 5 || strcmp(e->d_name + n - 4, ".job"))
            continue;
        r = &A.jobs[A.qcount];
        snprintf(mp, sizeof(mp), OAP_QUEUE_DIR "/%s", e->d_name);
        f = fopen(mp, "r");
        if (!f)
            continue;
        snprintf(r->path, sizeof(r->path), OAP_QUEUE_DIR "/%.*s.pdf", (int)(n - 4), e->d_name);
        snprintf(r->name, sizeof(r->name), "%.*s", (int)(n - 4), e->d_name);
        strcpy(r->state, "queued");
        while (fgets(line, sizeof(line), f))
            if (!strncmp(line, "state=", 6)) {
                char *p;
                copystr(r->state, sizeof(r->state), line + 6);
                p = strpbrk(r->state, "\r\n");
                if (p)
                    *p = 0;
            }
        fclose(f);
        A.qcount++;
    }
    closedir(d);
    qsort(A.jobs, (size_t)A.qcount, sizeof(A.jobs[0]), qcmp);
}

static int start_job(const char *action, const char *output, const char *source)
{
    OAVRequest r;
    char err[256];
    if (A.job_active) {
        char st[32], msg[256];
        if (oav_result(A.lastreq, st, sizeof(st), msg, sizeof(msg)) && oav_result_terminal(st))
            A.job_active = 0;
    }
    if (A.job_active) {
        status("Still preparing the last page; wait, or choose Stop preparing");
        return 0;
    }
    memset(&r, 0, sizeof(r));
    copystr(r.source, sizeof(r.source), source ? source : A.path);
    copystr(r.action, sizeof(r.action), action);
    if (output)
        copystr(r.output, sizeof(r.output), output);
    copystr(r.uri, sizeof(r.uri), A.printer_saved);
    r.layout = A.settings;
    if (oav_submit(&r, A.lastreq, sizeof(A.lastreq), err, sizeof(err))) {
        A.job_active = 1;
        set_gadget(B_CANCEL, GA_Disabled, FALSE);
        status(err);
        return 1;
    }
    status(err);
    return 0;
}

static int do_trigger(ULONG id)
{
    struct dtTrigger t;
    if (!A.dto) {
        status("Open a document first");
        return 0;
    }
    if (!trigger_supported(id)) {
        if (A.group == GID_ANIMATION && (id == STM_PAUSE || id == STM_STOP)) {
            struct adtStart a;
            a.MethodID = id == STM_PAUSE ? ADTM_PAUSE : ADTM_STOP;
            a.asa_Frame = 0;
            if (DoMethodA(A.dto, (Msg)&a))
                return 1;
        }
        status("This datatype does not advertise that playback/navigation action");
        return 0;
    }
    memset(&t, 0, sizeof(t));
    t.MethodID = DTM_TRIGGER;
    t.dtt_Function = id;
    DoDTMethodA(A.dto, A.win, NULL, (Msg)&t);
    return 1;
}

/* Print hands over to the Print requester (one way to print): a PDF as it
 * is, anything else as a PDF the worker makes first (see print_after). */
static int run_program(const char *program, const char *arg)
{
    char path[256], cmd[OAV_PATH_MAX + 300];
    BPTR in = Open((STRPTR)"NIL:", MODE_OLDFILE), out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    oap_program_path(program, path, sizeof(path));
    if (arg)
        snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", path, arg);
    else
        snprintf(cmd, sizeof(cmd), "\"%s\"", path);
    if (!in || !out || SystemTags((STRPTR)cmd, SYS_Asynch, TRUE, SYS_Input, in, SYS_Output, out, NP_StackSize, 65536, TAG_DONE) == -1) {
        if (in)
            Close(in);
        if (out)
            Close(out);
        return 0;
    }
    return 1;
}

static int print_dialog(void)
{
    if (!A.path[0]) {
        status("Open something to print first");
        return 0;
    }
    if (is_pdf(A.path)) {
        status(run_program("OpenPrint", A.path) ? "The Print window is open" : "Couldn't open OpenPrint");
        return 1;
    }
    if (A.group != GID_PICTURE) {
        status("Only pictures and PDFs can be printed from here for now");
        return 0;
    }
    if (!start_job("queue", NULL, NULL))
        return 0;
    A.print_after = 1;
    status("Preparing the page for printing...");
    return 1;
}

static int open_printers(void)
{
    status(run_program("OAPPrinters", NULL) ? "Printers and Queue is open" : "Couldn't open OAPPrinters");
    return 1;
}

/* The worker's answer; a page made for Print opens the Print requester. */
static void poll_jobs(void)
{
    char selected_printer[384], st[32], msg[256];
    if (oap_selected_printer(selected_printer, sizeof(selected_printer)))
        copystr(A.printer_saved, sizeof(A.printer_saved), selected_printer);
    if (!A.job_active || !oav_result(A.lastreq, st, sizeof(st), msg, sizeof(msg)))
        return;
    if (oav_result_terminal(st)) {
        A.job_active = 0;
        if (A.print_after) {
            const char *pdf = strstr(msg, "Queued: ");
            A.print_after = 0;
            if (pdf && run_program("OpenPrint", pdf + 8))
                copystr(msg, sizeof(msg), "The Print window is open for this page");
        }
        set_gadget(B_CANCEL, GA_Disabled, TRUE);
    }
    status(msg);
}

/* ---- actions (buttons, menus and ARexx alike) --------------------------------- */


/* ---- Find, in a text or an AmigaGuide file -------------------------------- */

#define FIND_LIMIT (8UL * 1024 * 1024)

/* The shown file's text, read from the file itself so every text datatype
 * (and AmigaGuide, which keeps no TDTA_Buffer) searches the same way. */
static int find_text(void)
{
    FILE *f;
    long n;
    if (A.find_buf)
        return 1;
    if (!A.dto || !A.path[0] || A.group == GID_PICTURE || A.group == GID_ANIMATION || A.group == GID_SOUND || is_pdf(A.path)) {
        status("Find works in text and AmigaGuide files");
        return 0;
    }
    if (!(f = fopen(A.path, "rb"))) {
        status("Cannot read the file to search it");
        return 0;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || (unsigned long)n > FIND_LIMIT || !(A.find_buf = malloc(n + 1))) {
        fclose(f);
        status(n > 0 ? "This file is too big to search (8 MB at most)" : "There is nothing in this file to search");
        return 0;
    }
    A.find_len = (long)fread(A.find_buf, 1, n, f);
    A.find_buf[A.find_len] = 0;
    fclose(f);
    A.find_guide = oav_is_guide(A.find_buf, A.find_len);
    A.find_pos = 0;
    return 1;
}

/* Shows line `line` near the top of the view, a little context above it. */
static void find_show_line(long line)
{
    ULONG total = 0;
    GetDTAttrs(A.dto, DTA_TotalVert, (ULONG)&total, TAG_DONE);
    if (line > 2)
        line -= 2;
    else
        line = 0;
    if (total && (ULONG)line >= total)
        line = total - 1;
    SetDTAttrs(A.dto, A.win, NULL, DTA_TopVert, line, TAG_DONE);
    A.refresh = 1;
}

/* A guide lays a node out after it opens: the line waits until the node is
 * laid out and long enough to hold it (or about five seconds). */
static void find_settle(void)
{
    ULONG total = 0, busy = 0;
    if (A.find_top < 0 || !A.dto)
        return;
    GetDTAttrs(A.dto, DTA_TotalVert, (ULONG)&total, DTA_Busy, (ULONG)&busy, TAG_DONE);
    if ((!busy && total > (ULONG)A.find_top) || ++A.find_wait > 50) {
        find_show_line(A.find_top);
        A.find_top = -1;
    }
}

static int find_next(void)
{
    OAVFound fd;
    char msg[200];
    if (!A.find_what[0])
        return 0;
    if (!find_text())
        return 0;
    if (!oav_find(A.find_buf, A.find_len, A.find_what, A.find_pos, A.find_guide, &fd)) {
        snprintf(msg, sizeof(msg), "\"%.60s\" is not in this file", A.find_what);
        status(msg);
        return 0;
    }
    A.find_pos = fd.at + 1;
    if (A.find_guide) {
        /* Another node: the guide opens again at it (DTM_GOTO from here
         * didn't move amigaguide.datatype on OS 3.2.3), the text kept. */
        STRPTR shown = NULL;                   /* the node on screen, if the datatype says */
        GetDTAttrs(A.dto, DTA_NodeName, (ULONG)&shown, TAG_DONE);
        if (strcasecmp(fd.node, shown && *shown ? (const char *)shown : A.find_node[0] ? A.find_node : "main")) {
            char *buf = A.find_buf, what[80];
            long len = A.find_len, pos = A.find_pos;
            int guide = A.find_guide;
            char path[OAV_PATH_MAX];
            copystr(what, sizeof(what), A.find_what);
            copystr(path, sizeof(path), A.path);
            copystr(A.open_node, sizeof(A.open_node), fd.node);
            A.find_buf = NULL;                 /* back below: the same file's text */
            load_file(path);
            A.open_node[0] = 0;
            A.find_buf = buf;
            A.find_len = len;
            A.find_pos = pos;
            A.find_guide = guide;
            copystr(A.find_what, sizeof(A.find_what), what);
            if (!A.dto)
                return 0;
        }
        copystr(A.find_node, sizeof(A.find_node), fd.node);
        A.find_top = fd.node_line;
        A.find_wait = 0;
        find_settle();
        snprintf(msg, sizeof(msg), "Found \"%.40s\" in %.40s, line %ld%s", A.find_what, fd.node, fd.node_line + 1,
                 fd.wrapped ? " (from the top again)" : "");
    } else {
        find_show_line(fd.line);
        snprintf(msg, sizeof(msg), "Found \"%.60s\" on line %ld%s", A.find_what, fd.line + 1,
                 fd.wrapped ? " (from the top again)" : "");
    }
    status(msg);
    return 1;
}

/* A small GadTools requester: what to find. 1 when Find was pressed. */
static int find_ask(void)
{
    enum { F_TEXT = 1, F_FIND, F_CANCEL };
    OAPGT *g = &A.g;
    struct Gadget *glist = NULL, *p, *str;
    struct Window *w;
    int bw = oap_gt_text_w(g, "Cancel") + 24, lw = oap_gt_text_w(g, "Find") + 8, iw = 2 * OAP_GT_MARGIN + lw + 30 * g->fixed_w;
    int gh = g->gad_h, ih = 2 * OAP_GT_MARGIN + 2 * gh + OAP_GT_GAP, done = 0, ok = 0;
    char title[120];
    /* The field starts empty (an OS 3 string gadget can't select the old
     * text for typing over); Return on its own finds the last text again. */
    if (A.find_what[0])
        snprintf(title, sizeof(title), "Find (Return alone: \"%.40s\")", A.find_what);
    else
        copystr(title, sizeof(title), "Find");
    if (iw < 2 * bw + 3 * OAP_GT_MARGIN)
        iw = 2 * bw + 3 * OAP_GT_MARGIN;
    w = OpenWindowTags(NULL, WA_Title, (ULONG)title, WA_PubScreen, (ULONG)g->screen, WA_InnerWidth, iw, WA_InnerHeight, ih,
        WA_Left, A.win->LeftEdge + (A.win->Width - iw) / 2, WA_Top, A.win->TopEdge + A.win->Height / 3,
        WA_Activate, TRUE, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SmartRefresh, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW | BUTTONIDCMP | STRINGIDCMP, TAG_DONE);
    if (!w)
        return 0;
    p = CreateContext(&glist);
    str = p = CreateGadget(STRING_KIND, p, oap_gt_ng(g, w->BorderLeft + OAP_GT_MARGIN + lw, w->BorderTop + OAP_GT_MARGIN,
                           iw - 2 * OAP_GT_MARGIN - lw, gh, "Find", F_TEXT, PLACETEXT_LEFT, 0),
                           GTST_String, (ULONG)"", GTST_MaxChars, sizeof(A.find_what) - 1, TAG_DONE);
    p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, w->BorderLeft + OAP_GT_MARGIN, w->BorderTop + ih - OAP_GT_MARGIN - gh, bw, gh,
                     "_Find", F_FIND, PLACETEXT_IN, 0), GT_Underscore, '_', TAG_DONE);
    p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, w->BorderLeft + iw - OAP_GT_MARGIN - bw, w->BorderTop + ih - OAP_GT_MARGIN - gh, bw, gh,
                     "_Cancel", F_CANCEL, PLACETEXT_IN, 0), GT_Underscore, '_', TAG_DONE);
    if (!p) {
        CloseWindow(w);
        FreeGadgets(glist);
        status("Not enough memory for the Find requester");
        return 0;
    }
    AddGList(w, glist, (UWORD)-1, -1, NULL);
    RefreshGList(glist, w, NULL, -1);
    GT_RefreshWindow(w, NULL);
    ActivateGadget(str, w, NULL);
    while (!done) {
        struct IntuiMessage *im;
        WaitPort(w->UserPort);
        while (!done && (im = GT_GetIMsg(w->UserPort)) != NULL) {
            ULONG class = im->Class;
            UWORD code = im->Code;
            struct Gadget *gg = (struct Gadget *)im->IAddress;
            GT_ReplyIMsg(im);
            if (class == IDCMP_CLOSEWINDOW)
                done = 1;
            else if (class == IDCMP_REFRESHWINDOW) {
                GT_BeginRefresh(w);
                GT_EndRefresh(w, TRUE);
            } else if (class == IDCMP_GADGETUP) {
                if (gg->GadgetID == F_CANCEL)
                    done = 1;
                else if (gg->GadgetID == F_FIND || (gg->GadgetID == F_TEXT && code != 9))   /* Return, not Tab */
                    done = ok = 1;
            } else if (class == IDCMP_VANILLAKEY) {
                if (code == 27 || (code | 0x20) == 'c')
                    done = 1;
                else if (code == 13 || (code | 0x20) == 'f')
                    done = ok = 1;
                else
                    ActivateGadget(str, w, NULL);
            }
        }
    }
    if (ok && ((struct StringInfo *)str->SpecialInfo)->Buffer[0])
        copystr(A.find_what, sizeof(A.find_what), (const char *)((struct StringInfo *)str->SpecialInfo)->Buffer);
    RemoveGList(w, glist, -1);
    CloseWindow(w);
    FreeGadgets(glist);
    return ok && A.find_what[0];
}

static int find_start(const char *what)
{
    char before[80];
    if (!A.dto) {
        status("Open a text or an AmigaGuide file first");
        return 0;
    }
    copystr(before, sizeof(before), A.find_what);
    if (what)
        copystr(A.find_what, sizeof(A.find_what), what);
    else if (!find_ask())
        return 0;
    if (A.find_buf && strcmp(before, A.find_what))
        A.find_pos = 0;                        /* new text: from the top; the same text goes on */
    return find_next();
}

static int action(int id, const char *arg)
{
    char path[OAV_PATH_MAX];
    switch (id) {
    case B_OPEN:
        if (arg)
            return load_file(arg);
        if (choose_file(path, 0))
            return load_file(path);
        return 0;
    case B_PAGE: A.page = !A.page; show_settings(); reload(); return 1;
    case B_FIT: A.page = 0; A.zoom = 0; show_settings(); reload(); return 1;
    case B_ONE: A.page = 0; A.zoom = 100; show_settings(); reload(); return 1;
    case B_MINUS:
        A.page = 0;
        A.zoom = A.zoom ? A.zoom - 25 : 75;
        if (A.zoom < 25)
            A.zoom = 25;
        show_settings();
        reload();
        return 1;
    case B_PLUS:
        A.page = 0;
        A.zoom = A.zoom ? A.zoom + 25 : 125;
        if (A.zoom > 400)
            A.zoom = 400;
        show_settings();
        reload();
        return 1;
    case B_PAPER: A.settings.paper = !A.settings.paper; show_settings(); reload(); return 1;
    case B_ORIENT: A.settings.landscape = !A.settings.landscape; show_settings(); reload(); return 1;
    case B_SCALE: A.settings.scale = !A.settings.scale; show_settings(); reload(); return 1;
    case B_EXPORT:
        if (!A.dto || A.group != GID_PICTURE) {
            status(is_pdf(A.path) ? "This is already a PDF: Print... can also save it elsewhere"
                                  : "Save as PDF... makes a PDF from a picture; open one first");
            return 0;
        }
        if (arg)
            return start_job("export", arg, NULL);
        if (choose_file(path, 1))
            return start_job("export", path, NULL);
        return 0;
    case B_PRINT: return start_job("queue", NULL, NULL);
    case B_PRINTDLG: return print_dialog();
    case B_REFRESH: scan_queue(); return 1;
    case B_PRINTERS: return open_printers();
    case B_CANCEL:
        if (A.job_active && oav_cancel(A.lastreq)) {
            A.print_after = 0;
            status("Stopping: the worker stops at its next safe point");
            return 1;
        }
        status("Nothing is being prepared");
        return 0;
    case B_PLAY: return do_trigger(STM_PLAY);
    case B_PAUSE: return do_trigger(STM_PAUSE);
    case B_STOP: return do_trigger(STM_STOP);
    case B_PREV: return do_trigger(STM_BROWSE_PREV);
    case B_NEXT: return do_trigger(STM_BROWSE_NEXT);
    case B_INFO: status(A.dto && A.infoline[0] ? A.infoline : oav_format_note(A.path)); return 1;
    case B_FIND: return find_start(arg);
    case B_FINDNEXT: return A.find_what[0] ? find_next() : find_start(NULL);
    case B_QUIT: A.running = 0; return 1;
    default: return 0;
    }
}

/* A cycle gadget moved: its new choice is `code`. */
static void cycle_moved(UWORD id, UWORD code)
{
    switch (id) {
    case B_VIEW: A.page = code == 0; break;
    case B_PAPER: A.settings.paper = code ? OAV_LETTER : OAV_A4; break;
    case B_ORIENT: A.settings.landscape = code != 0; break;
    case B_SCALE: A.settings.scale = code ? OAV_FILL : OAV_FIT; break;
    default: return;
    }
    reload();
}

static int set_option(const char *key, const char *val)
{
    OAVLayout proposed = A.settings;
    long margin;
    if (!key || !val)
        return 0;
    if (!strcasecmp(key, "PAPER")) {
        if (!strcasecmp(val, "A4")) proposed.paper = OAV_A4;
        else if (!strcasecmp(val, "LETTER")) proposed.paper = OAV_LETTER;
        else return 0;
    } else if (!strcasecmp(key, "ORIENTATION")) {
        if (!strcasecmp(val, "PORTRAIT")) proposed.landscape = 0;
        else if (!strcasecmp(val, "LANDSCAPE")) proposed.landscape = 1;
        else return 0;
    } else if (!strcasecmp(key, "SCALE")) {
        if (!strcasecmp(val, "FIT")) proposed.scale = OAV_FIT;
        else if (!strcasecmp(val, "FILL")) proposed.scale = OAV_FILL;
        else return 0;
    } else if (!strcasecmp(key, "MARGIN")) {
        if (!oav_parse_points(val, &margin))
            return 0;
        proposed.margin_cpt = margin;
    } else if (!strcasecmp(key, "PRINTER")) {
        if (!oav_safe_field(val) || strlen(val) >= 384)
            return 0;
        copystr(A.printer_saved, sizeof(A.printer_saved), val);
        return 1;
    } else
        return 0;
    A.settings = proposed;
    show_settings();
    reload();
    return 1;
}

/* ---- ARexx: the port OPENVIEW, and MultiView's MULTIVIEW.n ------------------ */

/* args: 0 none, 1 the rest of the line (needed), 2 two words, 3 the rest of the line (may be empty) */
static const struct { const char *name; int id; int args; } rexx_commands[] = {
    { "SET", RX_SET, 2 }, { "VERSION", RX_VERSION, 0 }, { "HELP", RX_HELP, 0 }, { "OPEN", B_OPEN, 3 },
    { "SAVEPDF", B_EXPORT, 1 }, { "PRINT", B_PRINT, 0 }, { "PLAY", B_PLAY, 0 }, { "PAUSE", B_PAUSE, 0 },
    { "STOP", B_STOP, 0 }, { "NEXT", B_NEXT, 0 }, { "PREVIOUS", B_PREV, 0 }, { "FIT", B_FIT, 0 },
    { "ACTUAL", B_ONE, 0 }, { "PAGE", B_PAGE, 0 }, { "PAPER", B_PAPER, 0 }, { "ORIENTATION", B_ORIENT, 0 },
    { "SCALE", B_SCALE, 0 }, { "JOB", RX_JOB, 0 }, { "JOBS", RX_JOBS, 0 }, { "CANCEL", B_CANCEL, 0 },
    { "REFRESH", B_REFRESH, 0 }, { "QUIT", B_QUIT, 0 }, { "FIND", B_FIND, 1 }, { "FINDNEXT", B_FINDNEXT, 0 },
    /* MultiView 47's commands (SYS:Locale/Help/english/Sys/Other/mvarexx.help) */
    { "RELOAD", RX_RELOAD, 0 }, { "SAVEAS", RX_SAVEAS, 3 }, { "ABOUT", RX_ABOUT, 0 }, { "COPY", RX_COPY, 0 },
    { "PASTE", RX_PASTE, 0 }, { "CLEARSELECTED", RX_CLEARSEL, 0 }, { "GETTRIGGERINFO", RX_GETTRIG, 3 },
    { "DOTRIGGERMETHOD", RX_DOTRIG, 3 }, { "SCREEN", RX_SCREEN, 3 }, { "PUBSCREEN", RX_PUBSCREEN, 3 },
    { "GETCURRENTDIR", RX_GETDIR, 0 }, { "GETFILEINFO", RX_GETFILE, 0 }, { "GETOBJECTINFO", RX_GETOBJ, 3 },
    { "MINIMUMSIZE", RX_MINSIZE, 0 }, { "NORMALSIZE", RX_NORMSIZE, 0 }, { "MAXIMUMSIZE", RX_MAXSIZE, 0 },
    { "WINDOWTOFRONT", RX_WTOFRONT, 0 }, { "WINDOWTOBACK", RX_WTOBACK, 0 }, { "SCREENTOFRONT", RX_STOFRONT, 0 },
    { "SCREENTOBACK", RX_STOBACK, 0 }, { "ACTIVATEWINDOW", RX_ACTIVATE, 0 }, { "BEEPSCREEN", RX_BEEP, 0 }
};

static struct RexxMsg *cur_rm;                 /* the message being answered: stems are set in its caller */

/* The next word of an ARexx command line, quotes allowed; `rest` takes
 * everything left (a file name with spaces needs no quotes then). */
static char *word(char **cursor, int rest)
{
    char *s = *cursor, *start;
    while (*s == ' ' || *s == '\t')
        s++;
    if (!*s)
        return NULL;
    if (*s == '"') {
        start = ++s;
        while (*s && *s != '"')
            s++;
        if (*s)
            *s++ = 0;
    } else if (rest) {
        start = s;
        s += strlen(s);
        while (s > start && (s[-1] == ' ' || s[-1] == '\t'))
            *--s = 0;
    } else {
        start = s;
        while (*s && *s != ' ' && *s != '\t')
            s++;
        if (*s)
            *s++ = 0;
    }
    *cursor = s;
    return start;
}

/* MultiView's keyword form "NAME file", "NAME=file" or "NAME "a file"", or the bare value. */
static char *keyword_value(char *rest, const char *key)
{
    size_t n = strlen(key), len;
    if (!rest)
        return NULL;
    while (*rest == ' ' || *rest == '\t')
        rest++;
    if (!strncasecmp(rest, key, n) && (rest[n] == ' ' || rest[n] == '\t' || rest[n] == '=')) {
        rest += n;
        if (*rest == '=')
            rest++;
        while (*rest == ' ' || *rest == '\t')
            rest++;
    }
    len = strlen(rest);
    if (len >= 2 && rest[0] == '"' && rest[len - 1] == '"') {
        rest[len - 1] = 0;
        rest++;
    }
    return *rest ? rest : NULL;
}

/* A keyword given as a switch, "CLIPBOARD" or "CLIPBOARD CLIPUNIT 1". */
static int starts_with_word(const char *s, const char *key)
{
    size_t n = strlen(key);
    return s && !strncasecmp(s, key, n) && (!s[n] || s[n] == ' ' || s[n] == '\t');
}

static const char *group_name(ULONG group)
{
    switch (group) {
    case GID_SYSTEM: return "System";
    case GID_TEXT: return "Text";
    case GID_DOCUMENT: return "Document";
    case GID_SOUND: return "Sound";
    case GID_INSTRUMENT: return "Instrument";
    case GID_MUSIC: return "Music";
    case GID_PICTURE: return "Picture";
    case GID_ANIMATION: return "Animation";
    case GID_MOVIE: return "Movie";
    default: return "";
    }
}

/* STEM.TAIL = value, in the caller's ARexx variables (a stem ends in a dot). */
static void set_stem(const char *stem, const char *tail, const char *value)
{
    char name[96];
    size_t n, i;
    if (!cur_rm || !RexxSysBase)
        return;
    snprintf(name, sizeof(name), "%.60s%s%.28s", stem, stem[strlen(stem) - 1] == '.' ? "" : ".", tail);
    n = strlen(name);
    for (i = 0; i < n; i++)
        if (name[i] >= 'a' && name[i] <= 'z')
            name[i] -= 32;
    SetRexxVar(cur_rm, (CONST_STRPTR)name, (CONST_STRPTR)value, (LONG)strlen(value));
}

/* GETOBJECTINFO: name, base name, group and ID of the datatype, quoted and comma separated (MultiView's VAR form,
 * which is also the plain one), or the stem FILENAME, NAME, BASENAME, GROUP, ID. */
static int rexx_objectinfo(char *rest)
{
    struct DataType *dt = NULL;
    char id[8] = { 0 }, *stem = NULL, *w;
    const char *name = "", *base = "", *group = "";
    ULONG gid = 0;
    if (!A.dto) {
        copystr(A.resultbuf, sizeof(A.resultbuf), "No object is open");
        return 10;
    }
    GetDTAttrs(A.dto, DTA_DataType, (ULONG)&dt, TAG_DONE);
    if (dt && dt->dtn_Header) {
        ULONG v = dt->dtn_Header->dth_ID;
        name = dt->dtn_Header->dth_Name ? (const char *)dt->dtn_Header->dth_Name : "";
        base = dt->dtn_Header->dth_BaseName ? (const char *)dt->dtn_Header->dth_BaseName : "";
        gid = dt->dtn_Header->dth_GroupID;
        group = group_name(gid);
        id[0] = (char)(v >> 24); id[1] = (char)(v >> 16); id[2] = (char)(v >> 8); id[3] = (char)v;
    }
    while (rest && (w = word(&rest, 0)) != NULL)
        if (!strcasecmp(w, "STEM"))
            stem = word(&rest, 0);
    if (stem) {
        set_stem(stem, "FILENAME", A.fullpath);
        set_stem(stem, "NAME", name);
        set_stem(stem, "BASENAME", base);
        set_stem(stem, "GROUP", group);
        set_stem(stem, "ID", id);
        A.resultbuf[0] = 0;
        return 0;
    }
    snprintf(A.resultbuf, sizeof(A.resultbuf), "\"%s\",\"%s\",\"%s\",\"%s\"", name, base, group, id);
    return 0;
}

/* GETTRIGGERINFO: the stem COUNT and n.LABEL, n.COMMAND, n.METHOD (n from 0), else lines "label,command,method". */
static int rexx_triggerinfo(char *rest)
{
    struct DTMethod *m;
    char *stem = NULL, *w, tail[32], num[16];
    int i, count = 0;
    size_t used = 0;
    if (!A.dto) {
        copystr(A.resultbuf, sizeof(A.resultbuf), "No object is open");
        return 10;
    }
    m = (struct DTMethod *)GetDTTriggerMethods(A.dto);
    while (m && count < 128 && m[count].dtm_Label)
        count++;
    while (rest && (w = word(&rest, 0)) != NULL)
        if (!strcasecmp(w, "STEM"))
            stem = word(&rest, 0);
    A.resultbuf[0] = 0;
    if (stem) {
        snprintf(num, sizeof(num), "%d", count);
        set_stem(stem, "COUNT", num);
    }
    for (i = 0; i < count; i++) {
        const char *label = m[i].dtm_Label ? (const char *)m[i].dtm_Label : "";
        const char *command = m[i].dtm_Command ? (const char *)m[i].dtm_Command : "";
        if (stem) {
            snprintf(tail, sizeof(tail), "%d.LABEL", i);
            set_stem(stem, tail, label);
            snprintf(tail, sizeof(tail), "%d.COMMAND", i);
            set_stem(stem, tail, command);
            snprintf(num, sizeof(num), "%lu", (unsigned long)m[i].dtm_Method);
            snprintf(tail, sizeof(tail), "%d.METHOD", i);
            set_stem(stem, tail, num);
        } else if (used < sizeof(A.resultbuf) - 160) {
            int len = snprintf(A.resultbuf + used, sizeof(A.resultbuf) - used, "%s,%s,%lu\n", label, command, (unsigned long)m[i].dtm_Method);
            if (len > 0)
                used += (size_t)len;
        }
    }
    return 0;
}

static void window_size(int which)
{
    struct Window *w = A.win;
    LONG top;
    if (!w)
        return;
    switch (which) {
    case RX_MINSIZE:
        ChangeWindowBox(w, w->LeftEdge, w->TopEdge, w->MinWidth, w->MinHeight);
        break;
    case RX_NORMSIZE:
        ChangeWindowBox(w, w->LeftEdge, w->TopEdge, A.normal_w, A.normal_h);
        break;
    default:
        top = A.g.screen->BarHeight + 1;
        ChangeWindowBox(w, 0, top, A.g.screen->Width, A.g.screen->Height - top);
        break;
    }
}

/* 0 done, 5 not here (said in the result), 10 failed. */
static int rexx_command(const char *line, const char **result)
{
    static char copy[OAV_PATH_MAX + 64];
    char *cursor = copy, *verb, *arg1 = NULL, *arg2 = NULL;
    size_t i;
    int ok = 1;
    copystr(copy, sizeof(copy), line);
    verb = word(&cursor, 0);
    *result = A.resultbuf;
    A.resultbuf[0] = 0;
    if (!verb)
        return 10;
    for (i = 0; i < sizeof(rexx_commands) / sizeof(rexx_commands[0]); i++)
        if (!strcasecmp(verb, rexx_commands[i].name))
            break;
    if (i == sizeof(rexx_commands) / sizeof(rexx_commands[0])) {
        copystr(A.resultbuf, sizeof(A.resultbuf), "Unknown command; HELP lists them");
        return 10;
    }
    if (rexx_commands[i].args == 1 && !(arg1 = word(&cursor, 1)))
        return 10;
    if (rexx_commands[i].args == 2 && (!(arg1 = word(&cursor, 0)) || !(arg2 = word(&cursor, 1))))
        return 10;
    if (rexx_commands[i].args == 3)
        arg1 = word(&cursor, 1);
    switch (rexx_commands[i].id) {
    case RX_VERSION:
        snprintf(A.resultbuf, sizeof(A.resultbuf), "OpenView %s GadTools", OAV_VERSION);
        break;
    case RX_HELP:
        copystr(A.resultbuf, sizeof(A.resultbuf), "OPEN [NAME] FILE | SET KEY VALUE | PRINT | SAVEPDF FILE | PLAY | PAUSE | STOP | NEXT | PREVIOUS | "
                "FIT | ACTUAL | PAGE | PAPER | ORIENTATION | SCALE | FIND TEXT | FINDNEXT | JOB | CANCEL | JOBS | REFRESH | QUIT | "
                "and MultiView's: RELOAD ABOUT GETFILEINFO GETCURRENTDIR GETOBJECTINFO GETTRIGGERINFO DOTRIGGERMETHOD "
                "MINIMUMSIZE NORMALSIZE MAXIMUMSIZE WINDOWTOFRONT WINDOWTOBACK SCREENTOFRONT SCREENTOBACK ACTIVATEWINDOW BEEPSCREEN "
                "SCREEN PUBSCREEN CLEARSELECTED (COPY PASTE SAVEAS answer 5: not here)");
        break;
    case RX_JOB: {
        char st[32], msg[256];
        if (!A.lastreq[0])
            copystr(A.resultbuf, sizeof(A.resultbuf), "none");
        else if (oav_result(A.lastreq, st, sizeof(st), msg, sizeof(msg)))
            snprintf(A.resultbuf, sizeof(A.resultbuf), "%s %s %s", st, A.lastreq, msg);
        else
            snprintf(A.resultbuf, sizeof(A.resultbuf), "running %s", A.lastreq);
        break;
    }
    case RX_JOBS: {
        int j;
        size_t n = 0;
        scan_queue();
        for (j = 0; j < A.qcount && n < sizeof(A.resultbuf) - 160; j++) {
            int len = snprintf(A.resultbuf + n, sizeof(A.resultbuf) - n, "%s %s\n", A.jobs[j].name, A.jobs[j].state);
            if (len > 0)
                n += (size_t)len;
        }
        break;
    }
    case RX_SET:
        ok = set_option(arg1, arg2);
        copystr(A.resultbuf, sizeof(A.resultbuf), ok ? "Settings applied" : "Invalid SET key or value");
        break;
    case B_OPEN: {
        char *name = keyword_value(arg1, "NAME");
        if (starts_with_word(name, "CLIPBOARD")) {
            copystr(A.resultbuf, sizeof(A.resultbuf), "OpenView does not show the Clipboard");
            return 5;
        }
        ok = action(B_OPEN, name);
        copystr(A.resultbuf, sizeof(A.resultbuf), A.message);
        break;
    }
    case RX_RELOAD:
        reload();
        break;
    case RX_ABOUT:
        ok = action(B_INFO, NULL);
        copystr(A.resultbuf, sizeof(A.resultbuf), A.message);
        break;
    case RX_SAVEAS:
        copystr(A.resultbuf, sizeof(A.resultbuf), "OpenView saves a picture as a PDF only (SAVEPDF)");
        return 5;
    case RX_COPY:
    case RX_PASTE:
        copystr(A.resultbuf, sizeof(A.resultbuf), "OpenView has no selection or Clipboard use");
        return 5;
    case RX_CLEARSEL:
    case RX_SCREEN:                     /* OpenView always shows its window on a public screen */
    case RX_PUBSCREEN:
        break;
    case RX_GETTRIG: return rexx_triggerinfo(arg1);
    case RX_DOTRIG: {
        char *num = keyword_value(arg1, "METHOD");
        if (!num)
            return 10;
        ok = do_trigger((ULONG)strtoul(num, NULL, 0) & STMF_METHOD_MASK);
        copystr(A.resultbuf, sizeof(A.resultbuf), A.message);
        break;
    }
    case RX_GETDIR: {
        STRPTR end;
        copystr(A.resultbuf, sizeof(A.resultbuf), A.fullpath);
        end = PathPart((STRPTR)A.resultbuf);
        *end = 0;
        break;
    }
    case RX_GETFILE:
        copystr(A.resultbuf, sizeof(A.resultbuf), A.fullpath);
        break;
    case RX_GETOBJ: return rexx_objectinfo(arg1);
    case RX_MINSIZE:
    case RX_NORMSIZE:
    case RX_MAXSIZE:
        window_size(rexx_commands[i].id);
        break;
    case RX_WTOFRONT: if (A.win) WindowToFront(A.win); break;
    case RX_WTOBACK: if (A.win) WindowToBack(A.win); break;
    case RX_STOFRONT: if (A.g.screen) ScreenToFront(A.g.screen); break;
    case RX_STOBACK: if (A.g.screen) ScreenToBack(A.g.screen); break;
    case RX_ACTIVATE: if (A.win) ActivateWindow(A.win); break;
    case RX_BEEP: DisplayBeep(A.g.screen); break;
    default:
        ok = action(rexx_commands[i].id, arg1);
        copystr(A.resultbuf, sizeof(A.resultbuf),
                (rexx_commands[i].id == B_PRINT || rexx_commands[i].id == B_EXPORT) && ok ? A.lastreq : A.message);
        break;
    }
    return ok ? 0 : 10;
}

static void rexx_messages(struct MsgPort *port)
{
    struct RexxMsg *rm;
    while (port && (rm = (struct RexxMsg *)GetMsg(port)) != NULL) {
        const char *result = NULL;
        int rc;
        cur_rm = rm;
        rc = rexx_command((const char *)rm->rm_Args[0], &result);
        cur_rm = NULL;
        rm->rm_Result1 = rc;
        rm->rm_Result2 = 0;
        if (rc == 0 && (rm->rm_Action & RXFF_RESULT) && RexxSysBase && result)
            rm->rm_Result2 = (LONG)CreateArgstring((STRPTR)result, (LONG)strlen(result));
        ReplyMsg((struct Message *)rm);
    }
}

/* A named port of ours, or NULL when the name is taken. */
static struct MsgPort *port_add(const char *name)
{
    struct MsgPort *port = CreateMsgPort();
    if (!port)
        return NULL;
    port->mp_Node.ln_Name = (char *)name;
    port->mp_Node.ln_Pri = 0;
    Forbid();
    if (FindPort((STRPTR)name)) {
        Permit();
        DeleteMsgPort(port);
        return NULL;
    }
    AddPort(port);
    Permit();
    return port;
}

/* OPENVIEW: one at a time, the first viewer's. MULTIVIEW.n (n from 1, the first free) or PORTNAME: every viewer's own,
 * the names MultiView's callers use. */
static void rexx_open(void)
{
    int n;
    if (!RexxSysBase)
        return;
    A.rexxport = port_add(REXX_NAME);
    if (A.opt.portname[0]) {
        copystr(A.mvname, sizeof(A.mvname), A.opt.portname);
        A.mvport = port_add(A.mvname);
    } else {
        for (n = 1; n < 100 && !A.mvport; n++) {
            snprintf(A.mvname, sizeof(A.mvname), "MULTIVIEW.%d", n);
            A.mvport = port_add(A.mvname);
        }
    }
}

static void port_remove(struct MsgPort *port)
{
    struct RexxMsg *rm;
    if (!port)
        return;
    RemPort(port);
    while ((rm = (struct RexxMsg *)GetMsg(port)) != NULL) {
        rm->rm_Result1 = RC_FATAL;
        rm->rm_Result2 = 0;
        ReplyMsg((struct Message *)rm);
    }
    DeleteMsgPort(port);
}

static void rexx_close(void)
{
    port_remove(A.rexxport);
    A.rexxport = NULL;
    port_remove(A.mvport);
    A.mvport = NULL;
}

/* ---- the window ------------------------------------------------------------- */

static int side_width(void)
{
    static const char *texts[] = { "Printers and queue...", "Stop preparing" };
    int w = oap_gt_text_w(&A.g, "Paper") + 8 + oap_gt_text_w(&A.g, "Fill and crop") + 40, i, t;
    for (i = 0; i < 2; i++)
        if ((t = oap_gt_text_w(&A.g, texts[i]) + 24) > w)
            w = t;
    return w + 2 * OAP_GT_INSET;
}

static void layout(int iw, int ih)
{
    Geo *G = &A.geo;
    int gh = A.g.gad_h;
    G->scroll_w = 18;
    G->scroll_h = 10 + (A.g.fh > 8 ? A.g.fh - 8 : 0);
    G->side_w = side_width();
    G->side_x = iw - OAP_GT_MARGIN - G->side_w;
    G->view_x = OAP_GT_MARGIN;
    G->view_y = OAP_GT_MARGIN + gh + OAP_GT_GAP;
    G->view_w = G->side_x - OAP_GT_GAP - G->view_x;
    G->y_status = ih - OAP_GT_MARGIN - gh;
    G->view_h = G->y_status - OAP_GT_GAP - G->view_y;
    G->y_setup = G->view_y;
    G->setup_h = A.g.fh + OAP_GT_GAP + 3 * (gh + 4) + OAP_GT_GAP - 4;
    /* the datatype draws inside the frame, clear of the scrollers */
    A.area.Left = A.win ? A.win->BorderLeft + G->view_x + 2 : 0;
    A.area.Top = A.win ? A.win->BorderTop + G->view_y + 1 : 0;
    A.area.Width = G->view_w - 4 - G->scroll_w;
    A.area.Height = G->view_h - 2 - G->scroll_h;
}

static void build(void)
{
    OAPGT *g = &A.g;
    Geo *G = &A.geo;
    struct Gadget *p;
    int bx = A.win->BorderLeft, by = A.win->BorderTop, gh = g->gad_h, x, w, i;
    static const struct { const char *text; UWORD id; } left[] = {
        { "_Open...", B_OPEN }, { "_Print...", B_PRINTDLG }, { "Save as P_DF...", B_EXPORT }
    };
    static const struct { const char *text; UWORD id; } zoom[] = {
        { "Fit", B_FIT }, { "1:1", B_ONE }, { "-", B_MINUS }, { "+", B_PLUS }
    };
    A.glist = NULL;
    p = CreateContext(&A.glist);
    /* the toolbar: open and print on the left, how to show it on the right */
    x = OAP_GT_MARGIN;
    for (i = 0; i < 3; i++) {
        w = oap_gt_text_w(g, left[i].text) + 16;
        p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, bx + x, by + OAP_GT_MARGIN, w, gh, left[i].text, left[i].id, PLACETEXT_IN, 0),
                         GT_Underscore, '_', TAG_DONE);
        x += w + 4;
    }
    x = G->side_x + G->side_w;
    for (i = 3; i >= 0; i--) {
        w = oap_gt_text_w(g, zoom[i].text) + 14;
        x -= w;
        p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, bx + x, by + OAP_GT_MARGIN, w, gh, zoom[i].text, zoom[i].id, PLACETEXT_IN, 0), TAG_DONE);
        x -= 2;
    }
    w = oap_gt_text_w(g, "As the page") + 36;
    x -= w + OAP_GT_GAP;
    p = CreateGadget(CYCLE_KIND, p, oap_gt_ng(g, bx + x, by + OAP_GT_MARGIN, w, gh, "S_how", B_VIEW, PLACETEXT_LEFT, 0),
                     GTCY_Labels, (ULONG)view_labels, GTCY_Active, A.page ? 0 : 1, GT_Underscore, '_', GA_Disabled, !A.setup_on, TAG_DONE);
    /* the scrollers, inside the view's frame */
    p = CreateGadget(SCROLLER_KIND, p, oap_gt_ng(g, bx + G->view_x + G->view_w - 2 - G->scroll_w, by + G->view_y + 1, G->scroll_w,
                     G->view_h - 2 - G->scroll_h, NULL, B_VSCROLL, 0, 0),
                     GTSC_Top, 0, GTSC_Total, 1, GTSC_Visible, 1, GTSC_Arrows, 9, PGA_Freedom, LORIENT_VERT, GA_RelVerify, TRUE,
                     GA_Immediate, TRUE, TAG_DONE);
    p = CreateGadget(SCROLLER_KIND, p, oap_gt_ng(g, bx + G->view_x + 2, by + G->view_y + G->view_h - 1 - G->scroll_h,
                     G->view_w - 4 - G->scroll_w, G->scroll_h, NULL, B_HSCROLL, 0, 0),
                     GTSC_Top, 0, GTSC_Total, 1, GTSC_Visible, 1, GTSC_Arrows, 16, PGA_Freedom, LORIENT_HORIZ, GA_RelVerify, TRUE,
                     GA_Immediate, TRUE, TAG_DONE);
    /* page setup */
    {
        static STRPTR *labels[] = { paper_labels, orient_labels, scale_labels };
        static const char *names[] = { "P_aper", "_Turn", "Si_ze" };
        static const UWORD ids[] = { B_PAPER, B_ORIENT, B_SCALE };
        int lw = oap_gt_text_w(g, "Paper"), cx, active[3];
        if (oap_gt_text_w(g, "Size") > lw) lw = oap_gt_text_w(g, "Size");
        if (oap_gt_text_w(g, "Turn") > lw) lw = oap_gt_text_w(g, "Turn");
        cx = G->side_x + OAP_GT_INSET + lw + 8;
        active[0] = A.settings.paper ? 1 : 0;
        active[1] = A.settings.landscape ? 1 : 0;
        active[2] = A.settings.scale ? 1 : 0;
        for (i = 0; i < 3; i++)
            p = CreateGadget(CYCLE_KIND, p, oap_gt_ng(g, bx + cx, by + G->y_setup + g->fh + OAP_GT_GAP + i * (gh + 4),
                             G->side_x + G->side_w - OAP_GT_INSET - cx, gh, names[i], ids[i], PLACETEXT_LEFT, 0),
                             GTCY_Labels, (ULONG)labels[i], GTCY_Active, active[i], GT_Underscore, '_', GA_Disabled, !A.setup_on, TAG_DONE);
    }
    p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, bx + G->side_x, by + G->y_status - OAP_GT_GAP - 2 * gh - 4, G->side_w, gh,
                     "P_rinters and queue...", B_PRINTERS, PLACETEXT_IN, 0), GT_Underscore, '_', TAG_DONE);
    p = CreateGadget(BUTTON_KIND, p, oap_gt_ng(g, bx + G->side_x, by + G->y_status - OAP_GT_GAP - gh, G->side_w, gh,
                     "_Stop preparing", B_CANCEL, PLACETEXT_IN, 0), GT_Underscore, '_', GA_Disabled, !A.job_active, TAG_DONE);
    p = CreateGadget(TEXT_KIND, p, oap_gt_ng(g, bx + OAP_GT_MARGIN, by + G->y_status, A.win->Width - A.win->BorderLeft - A.win->BorderRight - 2 * OAP_GT_MARGIN,
                     gh, NULL, B_STATUS, 0, 0), GTTX_Text, (ULONG)A.message, GTTX_Border, TRUE, GTTX_CopyText, TRUE, TAG_DONE);
    (void)p;
}

static void draw_static(void)
{
    Geo *G = &A.geo;
    struct RastPort *rp = A.win->RPort;
    int bx = A.win->BorderLeft, by = A.win->BorderTop;
    DrawBevelBox(rp, bx + G->view_x, by + G->view_y, G->view_w, G->view_h, GT_VisualInfo, (ULONG)A.g.vi, GTBB_Recessed, TRUE, TAG_DONE);
    oap_gt_group(&A.g, rp, bx + G->side_x, by + G->y_setup, G->side_w, G->setup_h, "Page setup");
    draw_view();
}

/* Gadgets for the window's size; the shown file is laid out again too. */
static void rebuild(void)
{
    close_content();
    if (A.glist) {
        RemoveGList(A.win, A.glist, -1);
        A.attached = 0;
        FreeGadgets(A.glist);
        A.glist = NULL;
    }
    oap_gt_erase(A.win);
    layout(A.win->Width - A.win->BorderLeft - A.win->BorderRight, A.win->Height - A.win->BorderTop - A.win->BorderBottom);
    build();
    if (!A.glist)
        return;
    AddGList(A.win, A.glist, ~0, -1, NULL);
    A.attached = 1;
    RefreshGList(A.glist, A.win, NULL, -1);
    GT_RefreshWindow(A.win, NULL);
    draw_static();
    reload();
}

static int libraries(void)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    UtilityBase = OpenLibrary((STRPTR)"utility.library", 39);
    DataTypesBase = OpenLibrary((STRPTR)"datatypes.library", 39);
    AslBase = OpenLibrary((STRPTR)"asl.library", 38);
    GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 39);
    WorkbenchBase = OpenLibrary((STRPTR)"workbench.library", 37);              /* optional: dropping files */
    RexxSysBase = (struct RxsLib *)OpenLibrary((STRPTR)"rexxsyslib.library", 36);   /* optional: ARexx */
    return IntuitionBase && GfxBase && UtilityBase && DataTypesBase && AslBase && GadToolsBase;
}

static void close_libraries(void)
{
    if (RexxSysBase) CloseLibrary((struct Library *)RexxSysBase);
    if (IconBase) CloseLibrary(IconBase);
    if (WorkbenchBase) CloseLibrary(WorkbenchBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (AslBase) CloseLibrary(AslBase);
    if (DataTypesBase) CloseLibrary(DataTypesBase);
    if (UtilityBase) CloseLibrary(UtilityBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
}

static void handle_window(void)
{
    struct IntuiMessage *im;
    while (A.running && (im = GT_GetIMsg(A.win->UserPort)) != NULL) {
        ULONG class = im->Class;
        UWORD code = im->Code;
        APTR address = im->IAddress;
        GT_ReplyIMsg(im);
        switch (class) {
        case IDCMP_CLOSEWINDOW:
            A.running = 0;
            break;
        case IDCMP_NEWSIZE:
            rebuild();
            break;
        case IDCMP_REFRESHWINDOW:
            GT_BeginRefresh(A.win);
            draw_static();
            GT_EndRefresh(A.win, TRUE);
            break;
        case IDCMP_IDCMPUPDATE:                /* the datatype changed what it shows */
            A.refresh = 1;
            find_settle();
            break;
        case IDCMP_INTUITICKS:
            find_settle();
            if (++A.ticks >= 10) {
                A.ticks = 0;
                poll_jobs();
            }
            break;
        case IDCMP_GADGETUP:
        case IDCMP_GADGETDOWN:
        case IDCMP_MOUSEMOVE: {
            struct Gadget *g = (struct Gadget *)address;
            UWORD id = class == IDCMP_MOUSEMOVE ? 0 : g->GadgetID;
            if (class == IDCMP_MOUSEMOVE) {
                /* a scroller being dragged reports through MOUSEMOVE */
                struct Gadget *v = gad(B_VSCROLL), *h = gad(B_HSCROLL);
                if (A.dto && v && (v->Flags & GFLG_SELECTED))
                    SetDTAttrs(A.dto, A.win, NULL, DTA_TopVert, code, TAG_DONE);
                else if (A.dto && h && (h->Flags & GFLG_SELECTED))
                    SetDTAttrs(A.dto, A.win, NULL, DTA_TopHoriz, code, TAG_DONE);
                break;
            }
            if (id == B_VSCROLL) {
                if (A.dto)
                    SetDTAttrs(A.dto, A.win, NULL, DTA_TopVert, code, TAG_DONE);
            } else if (id == B_HSCROLL) {
                if (A.dto)
                    SetDTAttrs(A.dto, A.win, NULL, DTA_TopHoriz, code, TAG_DONE);
            } else if (class == IDCMP_GADGETUP) {
                if (id == B_VIEW || id == B_PAPER || id == B_ORIENT || id == B_SCALE)
                    cycle_moved(id, code);
                else if (id && id < 1000)
                    action(id, NULL);
            }
            break;
        }
        case IDCMP_MENUPICK: {
            UWORD number = code;
            while (A.running && A.menu && number != MENUNULL) {
                struct MenuItem *item = ItemAddress(A.menu, number);
                if (!item)
                    break;
                action((int)(ULONG)GTMENUITEM_USERDATA(item), NULL);
                number = item->NextSelect;
            }
            break;
        }
        case IDCMP_VANILLAKEY:
            switch (code | 0x20) {
            case 'o': action(B_OPEN, NULL); break;
            case 'p': action(B_PRINTDLG, NULL); break;
            case 'd': action(B_EXPORT, NULL); break;
            case 'r': action(B_PRINTERS, NULL); break;
            case 's': if (A.job_active) action(B_CANCEL, NULL); break;
            case 'h': if (A.setup_on) action(B_PAGE, NULL); break;
            case 'a': if (A.setup_on) action(B_PAPER, NULL); break;
            case 't': if (A.setup_on) action(B_ORIENT, NULL); break;
            case 'z': if (A.setup_on) action(B_SCALE, NULL); break;
            case 'f': case '/': action(B_FIND, NULL); break;
            case 'n': action(B_FINDNEXT, NULL); break;
            }
            break;
        }
    }
}

/* ---- MultiView's command line and tool types ---------------------------------- */

static void options_defaults(Options *o)
{
    memset(o, 0, sizeof(*o));
    o->fontsize = o->left = o->top = o->width = o->height = -1;
}

/* MultiView's template, read whole: FILE and the keywords OpenView honours are kept, the others (CLIPUNIT, SCREEN,
 * BOOKMARK, BACKDROP, PRTUNIT, AUTORESIZE) are read and ignored. CLIPBOARD is read and answered in the status line. */
static int options_from_cli(Options *o)
{
    LONG args[20];
    struct RDArgs *rda;
    memset(args, 0, sizeof(args));
    rda = ReadArgs((STRPTR)MV_TEMPLATE, args, NULL);
    if (!rda) {
        PrintFault(IoErr(), (STRPTR)"OpenView");
        return 0;
    }
    if (args[0] && ((char *)args[0])[0])
        copystr(o->file, sizeof(o->file), (char *)args[0]);
    o->clipboard = args[1] != 0;
    if (args[4]) copystr(o->pubscreen, sizeof(o->pubscreen), (char *)args[4]);
    o->requester = args[5] != 0;
    if (args[7]) copystr(o->fontname, sizeof(o->fontname), (char *)args[7]);
    if (args[8]) o->fontsize = *(LONG *)args[8];
    o->window = args[10] != 0;
    if (args[11]) copystr(o->portname, sizeof(o->portname), (char *)args[11]);
    o->immediate = args[12] != 0;
    o->repeat = args[13] != 0;
    if (args[15]) o->left = *(LONG *)args[15];
    if (args[16]) o->top = *(LONG *)args[16];
    if (args[17]) o->width = *(LONG *)args[17];
    if (args[18]) o->height = *(LONG *)args[18];
    FreeArgs(rda);
    return 1;
}

/* Started from Workbench with a project: its icon's tool types are MultiView's keywords too. */
static void options_from_icon(Options *o, struct WBStartup *w)
{
    struct DiskObject *dobj;
    BPTR old;
    STRPTR *tt, v;
    if (w->sm_NumArgs < 2 || !w->sm_ArgList[1].wa_Lock || !w->sm_ArgList[1].wa_Name || !w->sm_ArgList[1].wa_Name[0])
        return;
    if (!IconBase && !(IconBase = OpenLibrary((STRPTR)"icon.library", 36)))
        return;
    old = CurrentDir(w->sm_ArgList[1].wa_Lock);
    dobj = GetDiskObject(w->sm_ArgList[1].wa_Name);
    CurrentDir(old);
    if (!dobj)
        return;
    tt = (STRPTR *)dobj->do_ToolTypes;
    if (tt) {
        if ((v = FindToolType(tt, (STRPTR)"PUBSCREEN"))) copystr(o->pubscreen, sizeof(o->pubscreen), (char *)v);
        if ((v = FindToolType(tt, (STRPTR)"PORTNAME"))) copystr(o->portname, sizeof(o->portname), (char *)v);
        if ((v = FindToolType(tt, (STRPTR)"FONTNAME"))) copystr(o->fontname, sizeof(o->fontname), (char *)v);
        if ((v = FindToolType(tt, (STRPTR)"FONTSIZE"))) o->fontsize = strtol((char *)v, NULL, 10);
        if ((v = FindToolType(tt, (STRPTR)"WINDOWLEFT"))) o->left = strtol((char *)v, NULL, 10);
        if ((v = FindToolType(tt, (STRPTR)"WINDOWTOP"))) o->top = strtol((char *)v, NULL, 10);
        if ((v = FindToolType(tt, (STRPTR)"WINDOWWIDTH"))) o->width = strtol((char *)v, NULL, 10);
        if ((v = FindToolType(tt, (STRPTR)"WINDOWHEIGHT"))) o->height = strtol((char *)v, NULL, 10);
        if (FindToolType(tt, (STRPTR)"IMMEDIATE")) o->immediate = 1;
        if (FindToolType(tt, (STRPTR)"REPEAT")) o->repeat = 1;
        if (FindToolType(tt, (STRPTR)"WINDOW")) o->window = 1;
        if (FindToolType(tt, (STRPTR)"REQUESTER")) o->requester = 1;
        if (FindToolType(tt, (STRPTR)"CLIPBOARD")) o->clipboard = 1;
    }
    FreeDiskObject(dobj);
}

static int viewer_main(int argc, char **argv)
{
    ULONG winsig, appsig = 0, rexxsig = 0, sig;
    char initial[OAV_PATH_MAX] = { 0 };
    int rc = 20, iw, ih;

    memset(&A, 0, sizeof(A));
    oav_layout_defaults(&A.settings);
    A.page = 1;
    A.find_top = -1;
    copystr(A.message, sizeof(A.message), "Open a picture or a PDF, or drop one on this window");
    if (!libraries()) {
        fputs("OpenView: needs AmigaOS 3.0 or later (datatypes, GadTools)\n", stderr);
        goto out;
    }
    /* after the libraries: libnix's string functions use utility.library (a trap otherwise) */
    options_defaults(&A.opt);
    if (argc > 0) {
        if (!options_from_cli(&A.opt)) {
            rc = 10;
            goto out;
        }
    } else {
        struct WBStartup *w = (struct WBStartup *)argv;
        if (w->sm_NumArgs > 1 && w->sm_ArgList[1].wa_Lock) {
            BPTR old = CurrentDir(w->sm_ArgList[1].wa_Lock);
            BPTR lock = Lock((STRPTR)w->sm_ArgList[1].wa_Name, ACCESS_READ);
            CurrentDir(old);
            if (lock) {
                if (NameFromLock(lock, (STRPTR)A.opt.file, sizeof(A.opt.file)) == 0)
                    A.opt.file[0] = 0;
                UnLock(lock);
            }
        }
        options_from_icon(&A.opt, w);
    }
    oap_gt_pubscreen = A.opt.pubscreen[0] ? A.opt.pubscreen : NULL;
    if (A.opt.fontname[0]) {
        size_t n = strlen(A.opt.fontname);
        snprintf(A.fontbuf, sizeof(A.fontbuf), "%.70s%s", A.opt.fontname,
                 n > 5 && !strcasecmp(A.opt.fontname + n - 5, ".font") ? "" : ".font");
        A.fontattr.ta_Name = (STRPTR)A.fontbuf;
        A.fontattr.ta_YSize = A.opt.fontsize > 0 ? (UWORD)A.opt.fontsize : 8;
    }
    if (!oap_gt_open(&A.g))
        goto out;
    A.whitepen = ObtainBestPen(A.g.screen->ViewPort.ColorMap, 0xffffffffUL, 0xffffffffUL, 0xffffffffUL, TAG_DONE);
    A.have_white = A.whitepen != (ULONG)-1;
    if (!A.have_white)
        A.whitepen = oap_gt_pen(&A.g, SHINEPEN);
    iw = 720;
    ih = 440;
    if (iw + 24 > A.g.screen->Width) iw = A.g.screen->Width - 24;
    if (ih + 40 > A.g.screen->Height) ih = A.g.screen->Height - 40;
    if (iw < 2 * side_width() + 120)
        oap_gt_fall_back(&A.g);                /* a small screen: Topaz 8 gives the picture more room */
    A.menu = CreateMenus(menus, TAG_DONE);
    if (A.menu)
        LayoutMenus(A.menu, A.g.vi, GTMN_NewLookMenus, TRUE, TAG_DONE);
    A.win = OpenWindowTags(NULL,
        WA_Title, (ULONG)"OpenView", WA_ScreenTitle, (ULONG)"OpenView: open, look, print",
        WA_PubScreen, (ULONG)A.g.screen,
        A.opt.width > 0 ? TAG_IGNORE : WA_InnerWidth, iw, A.opt.height > 0 ? TAG_IGNORE : WA_InnerHeight, ih,
        A.opt.width > 0 ? WA_Width : TAG_IGNORE, (ULONG)A.opt.width, A.opt.height > 0 ? WA_Height : TAG_IGNORE, (ULONG)A.opt.height,
        WA_Left, A.opt.left >= 0 ? A.opt.left : (A.g.screen->Width - iw) / 2, WA_Top, A.opt.top >= 0 ? A.opt.top : (A.g.screen->Height - ih) / 2,
        WA_Activate, TRUE, WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE, WA_SizeGadget, TRUE,
        WA_SizeBBottom, TRUE, WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_MOUSEMOVE | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW |
                  IDCMP_IDCMPUPDATE | IDCMP_INTUITICKS | IDCMP_MENUPICK | IDCMP_VANILLAKEY |
                  BUTTONIDCMP | CYCLEIDCMP | SCROLLERIDCMP | TEXTIDCMP,
        TAG_DONE);
    if (!A.win)
        goto out;
    WindowLimits(A.win, 2 * side_width() + 160 + A.win->BorderLeft + A.win->BorderRight,
                 A.g.fh * 12 + 160 + A.win->BorderTop + A.win->BorderBottom, ~0, ~0);
    A.normal_w = A.win->Width;
    A.normal_h = A.win->Height;
    if (A.menu)
        SetMenuStrip(A.win, A.menu);
    if (WorkbenchBase && (A.appport = CreateMsgPort()) != NULL)
        A.appwin = AddAppWindowA(0, 0, A.win, A.appport, NULL);
    rexx_open();
    rebuild();
    page_setup_applies(0);
    oap_selection_open(&A.selection);
    A.running = 1;

    copystr(initial, sizeof(initial), A.opt.file);
    if (initial[0])
        load_file(initial);
    if (A.opt.clipboard)
        status("OpenView does not show the Clipboard; open a file instead");

    winsig = 1UL << A.win->UserPort->mp_SigBit;
    if (A.appport)
        appsig = 1UL << A.appport->mp_SigBit;
    if (A.rexxport)
        rexxsig = 1UL << A.rexxport->mp_SigBit;
    if (A.mvport)
        rexxsig |= 1UL << A.mvport->mp_SigBit;
    /* as MultiView: no file and no WINDOW, or REQUESTER, asks which file; Cancel leaves the window empty */
    if ((!initial[0] && !A.opt.window && !A.opt.clipboard) || A.opt.requester)
        action(B_OPEN, NULL);
    while (A.running) {
        sig = Wait(winsig | appsig | rexxsig | oap_selection_mask(&A.selection) | SIGBREAKF_CTRL_C);
        if (sig & appsig) {
            struct AppMessage *am;
            char dropped[OAV_PATH_MAX];
            dropped[0] = 0;
            while ((am = (struct AppMessage *)GetMsg(A.appport))) {
                if (am->am_NumArgs > 0 && !dropped[0] && NameFromLock(am->am_ArgList[0].wa_Lock, (STRPTR)dropped, sizeof(dropped)))
                    AddPart((STRPTR)dropped, am->am_ArgList[0].wa_Name, sizeof(dropped));
                ReplyMsg((struct Message *)am);
            }
            if (dropped[0])
                load_file(dropped);
        }
        {
            char chosen[384];
            if (oap_selection_receive(&A.selection, chosen, sizeof(chosen)))
                copystr(A.printer_saved, sizeof(A.printer_saved), chosen);
        }
        if (sig & SIGBREAKF_CTRL_C)
            A.running = 0;
        if (sig & rexxsig) {
            rexx_messages(A.rexxport);
            rexx_messages(A.mvport);
        }
        if (sig & winsig)
            handle_window();
        if (A.refresh && A.dto) {
            A.refresh = 0;
            RefreshDTObjectA(A.dto, A.win, NULL, NULL);
            scroll_info();
        }
    }
    rc = 0;
out:
    oap_selection_close(&A.selection);
    rexx_close();
    if (A.appwin)
        RemoveAppWindow(A.appwin);
    if (A.appport) {
        struct Message *m;
        while ((m = GetMsg(A.appport)))
            ReplyMsg(m);
        DeleteMsgPort(A.appport);
    }
    close_content();
    if (A.win) {
        ClearMenuStrip(A.win);
        CloseWindow(A.win);
    }
    if (A.glist)
        FreeGadgets(A.glist);
    if (A.menu)
        FreeMenus(A.menu);
    if (A.have_white && A.g.screen)
        ReleasePen(A.g.screen->ViewPort.ColorMap, A.whitepen);
    oap_gt_close(&A.g);
    close_libraries();
    return rc;
}

int main(int argc, char **argv)
{
    return oap_main_with_stack(viewer_main, argc, argv, 65536);
}
