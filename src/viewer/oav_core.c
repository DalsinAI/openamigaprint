/* SPDX-License-Identifier: BSD-2-Clause */
#include "oav_core.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
void oav_layout_defaults(OAVLayout *s){memset(s,0,sizeof(*s));s->margin_pt=36.0;}
int oav_safe_field(const char *s)
{
    size_t n=0;if(!s||!*s)return 0;
    while(*s){unsigned char c=(unsigned char)*s++;if(c<32||c==127||++n>=OAV_PATH_MAX)return 0;}
    return 1;
}
static int extis(const char *s,const char *x)
{
    const char *p=strrchr(s?s:"",'.');if(!p)return 0;
    do {if(tolower((unsigned char)*p++)!=tolower((unsigned char)*x++))return 0;}while(*x);
    return *p==0;
}
const char *oav_format_note(const char *p)
{
    if(extis(p,".docx"))return "DOCX needs an OOXML document renderer; text extraction is not full layout.";
    if(extis(p,".pptx"))return "PPTX needs an OOXML slide renderer; slide text alone is not a slide.";
    if(extis(p,".webp"))return "WebP requires a picture provider; animated WebP additionally needs playback.";
    if(extis(p,".gif"))return "GIF loaded as a picture is still-only; animation requires an animation provider.";
    if(extis(p,".pdf"))return "PDF viewing depends on the installed PDF datatype; PDF reflow is not supported.";
    if(extis(p,".mp4")||extis(p,".avi")||extis(p,".mkv")||extis(p,".mov"))return "Video requires both a container reader and the codecs used inside this file.";
    return "A compatible installed datatype is required.";
}
int oav_place(const OAVLayout *s,unsigned long w,unsigned long h,OAVPlacement *p)
{
    double kx,ky,k,t;
    if(!s||!p||!w||!h||w>OAV_PIXEL_LIMIT/h)return 0;
    if(s->paper!=OAV_A4&&s->paper!=OAV_LETTER)return 0;
    if(s->landscape!=0&&s->landscape!=1)return 0;
    if(s->scale!=OAV_FIT&&s->scale!=OAV_FILL)return 0;
    p->page_w=s->paper==OAV_LETTER?612.0:595.2756;
    p->page_h=s->paper==OAV_LETTER?792.0:841.8898;
    if(s->landscape){t=p->page_w;p->page_w=p->page_h;p->page_h=t;}
    if(!(s->margin_pt>=0.0)||s->margin_pt*2>=p->page_w||s->margin_pt*2>=p->page_h)return 0;
    p->clip_x=p->clip_y=s->margin_pt;
    p->clip_w=p->page_w-2*s->margin_pt;p->clip_h=p->page_h-2*s->margin_pt;
    kx=p->clip_w/w;ky=p->clip_h/h;k=s->scale==OAV_FILL?(kx>ky?kx:ky):(kx<ky?kx:ky);
    p->w=w*k;p->h=h*k;p->x=(p->page_w-p->w)/2;p->y=(p->page_h-p->h)/2;return 1;
}
static void fail(char *e,size_t n,const char *s){if(n){strncpy(e,s,n-1);e[n-1]=0;}}
int oav_pdf_rgb(FILE *f,unsigned long w,unsigned long h,const OAVLayout *s,
               OAVReadRow readrow,void *ctx,char *err,size_t cap)
{
    OAVPlacement p;long off[7],xref;unsigned char *row=NULL;unsigned long y;
    char content[512];int clen,i;size_t rowbytes;
    if(!f||!readrow||!oav_place(s,w,h,&p)){fail(err,cap,"Invalid dimensions or page layout");return 0;}
    if(w>SIZE_MAX/3){fail(err,cap,"Image row exceeds address range");return 0;}
    rowbytes=(size_t)w*3;row=malloc(rowbytes);if(!row){fail(err,cap,"Not enough memory for image row");return 0;}
    clen=snprintf(content,sizeof(content),"q\n%.4f %.4f %.4f %.4f re W n\n%.4f 0 0 %.4f %.4f %.4f cm\n/Im0 Do\nQ\n",
        p.clip_x,p.clip_y,p.clip_w,p.clip_h,p.w,p.h,p.x,p.y);
    if(clen<0||(size_t)clen>=sizeof(content)){free(row);fail(err,cap,"Page command overflow");return 0;}
    fputs("%PDF-1.4\n% OpenAmigaView RGB export\n",f);
    off[1]=ftell(f);fputs("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",f);
    off[2]=ftell(f);fputs("2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n",f);
    off[3]=ftell(f);fprintf(f,"3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.4f %.4f] /Resources << /XObject << /Im0 5 0 R >> >> /Contents 4 0 R >>\nendobj\n",p.page_w,p.page_h);
    off[4]=ftell(f);fprintf(f,"4 0 obj\n<< /Length %d >>\nstream\n",clen);fwrite(content,1,(size_t)clen,f);fputs("\nendstream\nendobj\n",f);
    off[5]=ftell(f);fprintf(f,"5 0 obj\n<< /Type /XObject /Subtype /Image /Width %lu /Height %lu /ColorSpace /DeviceRGB /BitsPerComponent 8 /Length %lu >>\nstream\n",w,h,w*h*3);
    for(y=0;y<h;y++){
        if(!readrow(ctx,y,row,rowbytes)){free(row);fail(err,cap,"Decode cancelled or pixel read failed");return 0;}
        if(fwrite(row,1,rowbytes,f)!=rowbytes){free(row);fail(err,cap,"Output write failed");return 0;}
    }
    free(row);fputs("\nendstream\nendobj\n",f);
    xref=ftell(f);fputs("xref\n0 6\n0000000000 65535 f \n",f);
    for(i=1;i<=5;i++){if(off[i]<0){fail(err,cap,"Output position failed");return 0;}fprintf(f,"%010ld 00000 n \n",off[i]);}
    fprintf(f,"trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n",xref);
    if(xref<0||ferror(f)||fflush(f)){fail(err,cap,"PDF finalisation failed");return 0;}
    fail(err,cap,"Saved");return 1;
}
