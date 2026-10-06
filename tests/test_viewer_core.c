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
 {
  static const char txt[]="One line\nTwo LINES here\nthree\nM\xc4" "dchen line\n";
  static const char guide[]="@database x.guide\n@node Main \"Contents\"\n@title Hello\nFirst main line\n@{\"Next\" link More}\n@endnode\n"
   "@node More\nalpha\n@remark line not shown\nbeta line\n@endnode\n";
  OAVFound fd;long gl=(long)strlen(guide),tl=(long)strlen(txt);
  assert(!oav_is_guide(txt,tl));assert(oav_is_guide(guide,gl));
  assert(oav_find(txt,tl,"line",0,0,&fd)&&fd.line==0&&fd.at==4&&!fd.wrapped);
  assert(oav_find(txt,tl,"line",fd.at+1,0,&fd)&&fd.line==1);
  assert(oav_find(txt,tl,"m\xe4" "dchen",0,0,&fd)&&fd.line==3);
  assert(oav_find(txt,tl,"one",10,0,&fd)&&fd.wrapped&&fd.line==0);
  assert(!oav_find(txt,tl,"absent",0,0,&fd)&&fd.at<0);assert(!oav_find(txt,tl,"",0,0,&fd));
  assert(oav_find(guide,gl,"hello",0,1,&fd)==0);
  assert(oav_find(guide,gl,"main",0,1,&fd)&&!strcmp(fd.node,"Main")&&fd.node_line==0);
  assert(oav_find(guide,gl,"next",0,1,&fd)&&!strcmp(fd.node,"Main")&&fd.node_line==1);
  assert(oav_find(guide,gl,"line",fd.at,1,&fd)&&!strcmp(fd.node,"More")&&fd.node_line==1&&!strncmp(guide+fd.at-5,"beta",4));
  assert(oav_find(guide,gl,"more",0,1,&fd)&&!strcmp(fd.node,"Main"));   /* the link's target, shown as text */
 }
 puts("PASS: layout, fit/fill, aspect, dimensions, field validation, PDF output and cancellation, find in text and guides");return 0;
}
