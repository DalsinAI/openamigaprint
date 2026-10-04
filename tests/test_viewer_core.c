#include "oav_core.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static int rows(void *ctx,unsigned long y,unsigned char *p,size_t n)
{size_t i;(void)ctx;for(i=0;i<n;i+=3){p[i]=(unsigned char)(y*4);p[i+1]=(unsigned char)i;p[i+2]=180;}return 1;}
static int cancelled(void *ctx,unsigned long y,unsigned char *p,size_t n)
{(void)ctx;(void)y;(void)p;(void)n;return 0;}
int main(void)
{
 OAVLayout s;OAVPlacement p;FILE *f;char err[120];
 oav_layout_defaults(&s);assert(oav_place(&s,320,200,&p));assert(labs(p.w*200-p.h*320)<=320);
 assert(p.w<=p.clip_w+0.001&&p.h<=p.clip_h+0.001);assert(p.x>=s.margin_cpt-0.001);
 s.landscape=1;assert(oav_place(&s,200,320,&p));assert(p.page_w>p.page_h);
 s.scale=OAV_FILL;assert(oav_place(&s,320,200,&p));assert(p.w>=p.clip_w-0.001&&p.h>=p.clip_h-0.001);
 assert(!oav_place(&s,0,10,&p));assert(!oav_place(&s,0xffffffffUL,0xffffffffUL,&p));
 s.margin_cpt=100000;assert(!oav_place(&s,10,10,&p));oav_layout_defaults(&s);
 assert(oav_safe_field("Work:My picture.png"));assert(!oav_safe_field("bad\nfield"));
 assert(strstr(oav_format_note("hello.PPTX"),"slide renderer"));
 f=fopen("build/viewer-core-test.pdf","wb");assert(f);assert(oav_pdf_rgb(f,96,64,&s,rows,NULL,err,sizeof(err)));assert(!fclose(f));
 f=tmpfile();assert(f);assert(!oav_pdf_rgb(f,96,64,&s,cancelled,NULL,err,sizeof(err)));fclose(f);
 puts("PASS: layout, fit/fill, aspect, dimensions, field validation, PDF output and cancellation");return 0;
}
