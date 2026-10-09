/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#ifndef OAV_CORE_H
#define OAV_CORE_H
#include <stddef.h>
#include <stdio.h>
#define OAV_PATH_MAX 512
#define OAV_DIM_LIMIT 16384UL
#define OAV_PIXEL_LIMIT 16777216UL
#define OAV_FILE_LIMIT 67108864UL
#define OAV_VERSION "0.5"
#define OAV_REQUEST_DIR "SYS:Spool/OpenView"
enum { OAV_A4, OAV_LETTER };
enum { OAV_FIT, OAV_FILL };
typedef struct OAVLayout { int paper, landscape, scale; long margin_cpt; } OAVLayout;
typedef struct OAVPlacement { long page_w,page_h,x,y,w,h,clip_x,clip_y,clip_w,clip_h; } OAVPlacement;
typedef int (*OAVReadRow)(void *ctx,unsigned long row,unsigned char *rgb,size_t bytes);
long oav_scale(long value,long numerator,long denominator);
int oav_parse_points(const char *s,long *cpt);
void oav_layout_defaults(OAVLayout *s);
int oav_place(const OAVLayout *s,unsigned long w,unsigned long h,OAVPlacement *p);
int oav_safe_field(const char *s);
const char *oav_format_note(const char *path);
/* Find: where `what` is in a text or an AmigaGuide file, ignoring case
 * (Latin-1 letters too). The search starts at `from` and goes round to the
 * start once. A guide counts lines the way it is shown: inside a node, with
 * @ command lines left out. 1 when found. */
typedef struct OAVFound { long at, line, node_line; int wrapped; char node[64]; } OAVFound;
int oav_is_guide(const char *buf,long len);
int oav_find(const char *buf,long len,const char *what,long from,int guide,OAVFound *f);
int oav_pdf_rgb(FILE *f,unsigned long w,unsigned long h,const OAVLayout *s,
                OAVReadRow readrow,void *ctx,char *err,size_t errcap);
#endif
