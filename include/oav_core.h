/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef OAV_CORE_H
#define OAV_CORE_H
#include <stddef.h>
#include <stdio.h>
#define OAV_PATH_MAX 512
#define OAV_PIXEL_LIMIT 16777216UL
#define OAV_FILE_LIMIT 67108864UL
#define OAV_VERSION "0.2.0-alpha1"
#define OAV_REQUEST_DIR "SYS:Spool/OpenAmigaView"
enum { OAV_A4, OAV_LETTER };
enum { OAV_FIT, OAV_FILL };
typedef struct OAVLayout { int paper, landscape, scale; double margin_pt; } OAVLayout;
typedef struct OAVPlacement { double page_w,page_h,x,y,w,h,clip_x,clip_y,clip_w,clip_h; } OAVPlacement;
typedef int (*OAVReadRow)(void *ctx,unsigned long row,unsigned char *rgb,size_t bytes);
void oav_layout_defaults(OAVLayout *s);
int oav_place(const OAVLayout *s,unsigned long w,unsigned long h,OAVPlacement *p);
int oav_safe_field(const char *s);
const char *oav_format_note(const char *path);
int oav_pdf_rgb(FILE *f,unsigned long w,unsigned long h,const OAVLayout *s,
                OAVReadRow readrow,void *ctx,char *err,size_t errcap);
#endif
