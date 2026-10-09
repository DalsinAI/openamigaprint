/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* OpenPrint's GadTools helpers (include/oap_gt.h). */
#include "oap_gt.h"
#include <exec/libraries.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <stdio.h>
#include <string.h>

extern struct GfxBase *GfxBase;
extern struct Library *GadToolsBase;

static struct TextAttr topaz8 = { (STRPTR)"topaz.font", 8, FS_NORMAL, FPF_ROMFONT };

static void metrics(OAPGT *g)
{
    InitRastPort(&g->measure);
    SetFont(&g->measure, g->font);
    g->fh = g->font->tf_YSize;
    g->line_h = g->fh + 2;
    g->gad_h = g->fh + 6;
}

int oap_gt_open(OAPGT *g)
{
    struct TextFont *df;
    memset(g, 0, sizeof(*g));
    g->screen = LockPubScreen(NULL);
    if (!g->screen)
        return 0;
    g->vi = GetVisualInfo(g->screen, TAG_DONE);
    g->dri = GetScreenDrawInfo(g->screen);
    g->attr = *g->screen->Font;
    g->font = OpenFont(&g->attr);
    if (!g->font) {
        g->attr = topaz8;
        g->font = OpenFont(&g->attr);
    }
    /* the system's default font is fixed width: lists with columns use it */
    df = GfxBase->DefaultFont;
    g->fixed_attr.ta_Name = (STRPTR)df->tf_Message.mn_Node.ln_Name;
    g->fixed_attr.ta_YSize = df->tf_YSize;
    g->fixed_attr.ta_Style = df->tf_Style;
    g->fixed_attr.ta_Flags = df->tf_Flags;
    g->fixed = OpenFont(&g->fixed_attr);
    if (!g->fixed || (g->fixed->tf_Flags & FPF_PROPORTIONAL)) {
        if (g->fixed)
            CloseFont(g->fixed);
        g->fixed_attr = topaz8;
        g->fixed = OpenFont(&g->fixed_attr);
    }
    if (!g->vi || !g->font || !g->fixed)
        return 0;
    g->fixed_w = g->fixed->tf_XSize;
    metrics(g);
    return 1;
}

int oap_gt_fall_back(OAPGT *g)
{
    struct TextFont *f;
    if (g->attr.ta_YSize == 8 && !strcmp((char *)g->attr.ta_Name, "topaz.font"))
        return 0;
    f = OpenFont(&topaz8);
    if (!f)
        return 0;
    CloseFont(g->font);
    g->font = f;
    g->attr = topaz8;
    metrics(g);
    return 1;
}

void oap_gt_close(OAPGT *g)
{
    if (g->fixed)
        CloseFont(g->fixed);
    if (g->font)
        CloseFont(g->font);
    if (g->vi)
        FreeVisualInfo(g->vi);
    if (g->dri)
        FreeScreenDrawInfo(g->screen, g->dri);
    if (g->screen)
        UnlockPubScreen(NULL, g->screen);
    memset(g, 0, sizeof(*g));
}

int oap_gt_text_w(OAPGT *g, const char *s)
{
    char plain[160];
    int n = 0;
    for (; s && *s && n < (int)sizeof(plain) - 1; ++s)
        if (*s != '_')
            plain[n++] = *s;
    return TextLength(&g->measure, (STRPTR)plain, n);
}

struct NewGadget *oap_gt_ng(OAPGT *g, int x, int y, int w, int h, const char *label, UWORD id, ULONG flags, int fixed)
{
    static struct NewGadget ng;
    memset(&ng, 0, sizeof(ng));
    ng.ng_LeftEdge = x;
    ng.ng_TopEdge = y;
    ng.ng_Width = w;
    ng.ng_Height = h;
    ng.ng_GadgetText = (UBYTE *)label;
    ng.ng_TextAttr = fixed ? &g->fixed_attr : &g->attr;
    ng.ng_GadgetID = id;
    ng.ng_Flags = flags;
    ng.ng_VisualInfo = g->vi;
    return &ng;
}

UWORD oap_gt_pen(OAPGT *g, int which)
{
    static const UWORD fallback[] = { 0, 1, 1, 2, 1, 3, 1, 0, 2 };   /* DETAILPEN .. HIGHLIGHTTEXTPEN */
    if (g->dri && which < (int)g->dri->dri_NumPens)
        return g->dri->dri_Pens[which];
    return which < (int)(sizeof(fallback) / sizeof(fallback[0])) ? fallback[which] : 1;
}

void oap_gt_text(OAPGT *g, struct RastPort *rp, int x, int y, const char *s, int pen, int max_w)
{
    struct TextExtent te;
    ULONG n = strlen(s);
    SetFont(rp, g->font);
    SetAPen(rp, oap_gt_pen(g, pen));
    SetDrMd(rp, JAM1);
    if (max_w > 0)
        n = TextFit(rp, (STRPTR)s, n, &te, NULL, 1, max_w, g->fh + 1);
    Move(rp, x, y + g->font->tf_Baseline);
    Text(rp, (STRPTR)s, n);
}

/* A ridged frame with its title in a gap at the top, as the Prefs editors
 * draw their groups. On 2.04 the ridge is two boxes. */
void oap_gt_group(OAPGT *g, struct RastPort *rp, int x, int y, int w, int h, const char *title)
{
    int band = title ? g->fh / 2 : 0;
    if (GadToolsBase->lib_Version >= 39)
        DrawBevelBox(rp, x, y + band, w, h - band, GT_VisualInfo, (ULONG)g->vi, GTBB_FrameType, BBFT_RIDGE, TAG_DONE);
    else {
        DrawBevelBox(rp, x, y + band, w, h - band, GT_VisualInfo, (ULONG)g->vi, GTBB_Recessed, TRUE, TAG_DONE);
        DrawBevelBox(rp, x + 1, y + band + 1, w - 2, h - band - 2, GT_VisualInfo, (ULONG)g->vi, TAG_DONE);
    }
    if (title) {
        int tw = oap_gt_text_w(g, title);
        SetAPen(rp, oap_gt_pen(g, BACKGROUNDPEN));
        RectFill(rp, x + (w - tw) / 2 - 4, y, x + (w + tw) / 2 + 3, y + g->fh - 1);
        oap_gt_text(g, rp, x + (w - tw) / 2, y, title, TEXTPEN, 0);
    }
}

/* A recessed bar filled to `percent`, with the number in it. */
void oap_gt_progress(OAPGT *g, struct RastPort *rp, int x, int y, int w, int h, int percent, int active)
{
    char text[8];
    int fill, tw;
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    DrawBevelBox(rp, x, y, w, h, GT_VisualInfo, (ULONG)g->vi, GTBB_Recessed, TRUE, TAG_DONE);
    SetAPen(rp, oap_gt_pen(g, BACKGROUNDPEN));
    RectFill(rp, x + 2, y + 1, x + w - 3, y + h - 2);
    fill = (w - 4) * percent / 100;
    if (active && fill > 0) {
        SetAPen(rp, oap_gt_pen(g, FILLPEN));
        RectFill(rp, x + 2, y + 1, x + 1 + fill, y + h - 2);
    }
    snprintf(text, sizeof(text), "%d%%", percent);
    tw = oap_gt_text_w(g, text);
    oap_gt_text(g, rp, x + (w - tw) / 2, y + (h - g->fh) / 2, text, active ? TEXTPEN : SHADOWPEN, 0);
}

void oap_gt_erase(struct Window *win)
{
    EraseRect(win->RPort, win->BorderLeft, win->BorderTop,
              win->Width - win->BorderRight - 1, win->Height - win->BorderBottom - 1);
}

struct Gadget *oap_gt_find(struct Gadget *list, UWORD id)
{
    /* The last with the ID: a GadTools kind made of several gadgets (a
     * list view: its list, scroller and arrows) gives each the ID, and the
     * one CreateGadget answered, which GT_SetGadgetAttrs needs, comes last.
     * The first was the list view's inner part, so new labels never showed. */
    struct Gadget *found = NULL;
    for (; list; list = list->NextGadget)
        if (list->GadgetID == id)
            found = list;
    return found;
}
