/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#ifndef OAP_GT_H
#define OAP_GT_H
/* What OpenPrint's GadTools windows share: the screen, its font (or
 * Topaz 8 when a window would not fit), a fixed-width font for lists with
 * columns, and the drawing GadTools leaves to the program: titled groups,
 * a progress bar and plain text. Dale, 4 October 2026: OS 3.x applications
 * use GadTools (or MUI), not ReAction. */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/text.h>
#include <graphics/rastport.h>
#include <libraries/gadtools.h>

#define OAP_GT_MARGIN 8      /* inside the window's borders */
#define OAP_GT_GAP    6      /* between gadgets and groups */
#define OAP_GT_INSET  10     /* from a group's ridge to its contents */

typedef struct OAPGT {
    struct Screen *screen;
    APTR vi;
    struct DrawInfo *dri;
    struct TextAttr attr;            /* the window's font */
    struct TextFont *font;
    struct TextAttr fixed_attr;      /* fixed width, for columns in lists */
    struct TextFont *fixed;
    struct RastPort measure;
    int fh;                          /* font height */
    int line_h;                      /* a line of text */
    int gad_h;                       /* a button, cycle or string gadget */
    int fixed_w;                     /* one fixed-width character */
} OAPGT;

/* Locks the default public screen and takes its font. 0 on failure. */
int oap_gt_open(OAPGT *g);
/* The window would not fit: Topaz 8 from now on. 0 when already Topaz. */
int oap_gt_fall_back(OAPGT *g);
void oap_gt_close(OAPGT *g);

/* Text width in the window's font; '_' (the shortcut mark) takes no room. */
int oap_gt_text_w(OAPGT *g, const char *s);
/* A NewGadget for CreateGadget, in the window's font (or the fixed one). */
struct NewGadget *oap_gt_ng(OAPGT *g, int x, int y, int w, int h, const char *label, UWORD id, ULONG flags, int fixed);

/* Drawing, into the window's RastPort: */
void oap_gt_group(OAPGT *g, struct RastPort *rp, int x, int y, int w, int h, const char *title);
void oap_gt_text(OAPGT *g, struct RastPort *rp, int x, int y, const char *s, int pen, int max_w);
void oap_gt_progress(OAPGT *g, struct RastPort *rp, int x, int y, int w, int h, int percent, int active);
void oap_gt_erase(struct Window *win);

/* The pen numbers, from the screen's DrawInfo. */
UWORD oap_gt_pen(OAPGT *g, int which);

/* The first gadget in a list with this ID, or NULL. */
struct Gadget *oap_gt_find(struct Gadget *list, UWORD id);
#endif
