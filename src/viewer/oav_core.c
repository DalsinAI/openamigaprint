/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#include "oav_core.h"
#include "oap_str.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
void oav_layout_defaults(OAVLayout *s){memset(s,0,sizeof(*s));s->margin_cpt=3600;}
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
/* Geometry is hundredths of a point; no FPU or math-library dependency. */
long oav_scale(long value,long numerator,long denominator)
{
 long sign=value<0?-1:1;unsigned long v=(unsigned long)(value<0?-value:value);
 if(numerator<0||denominator<=0)return 0;
 return sign*(long)((v/(unsigned long)denominator)*(unsigned long)numerator+
       ((v%(unsigned long)denominator)*(unsigned long)numerator)/(unsigned long)denominator);
}
int oav_parse_points(const char *s,long *cpt)
{
 unsigned long whole=0,frac=0,n=0;if(!s||!cpt||!*s)return 0;
 while(*s>='0'&&*s<='9'){whole=whole*10+(unsigned long)(*s++-'0');if(whole>144)return 0;n++;}
 if(!n)return 0;
 if(*s=='.'){s++;if(*s<'0'||*s>'9')return 0;frac=(unsigned long)(*s++-'0')*10;if(*s>='0'&&*s<='9')frac+=(unsigned long)(*s++-'0');}
 if(*s||whole*100+frac>14400)return 0;
 *cpt=(long)(whole*100+frac);return 1;
}
int oav_place(const OAVLayout *s,unsigned long w,unsigned long h,OAVPlacement *p)
{
 long t;int bywidth;
 if(!s||!p||!w||!h||w>OAV_DIM_LIMIT||h>OAV_DIM_LIMIT||w>OAV_PIXEL_LIMIT/h)return 0;
 if(s->paper!=OAV_A4&&s->paper!=OAV_LETTER)return 0;
 if(s->landscape!=0&&s->landscape!=1)return 0;
 if(s->scale!=OAV_FIT&&s->scale!=OAV_FILL)return 0;
 p->page_w=s->paper==OAV_LETTER?61200:59528;p->page_h=s->paper==OAV_LETTER?79200:84189;
 if(s->landscape){t=p->page_w;p->page_w=p->page_h;p->page_h=t;}
 if(s->margin_cpt<0||s->margin_cpt>=p->page_w/2||s->margin_cpt>=p->page_h/2)return 0;
 p->clip_x=p->clip_y=s->margin_cpt;p->clip_w=p->page_w-2*s->margin_cpt;p->clip_h=p->page_h-2*s->margin_cpt;
 bywidth=(unsigned long)p->clip_w*h<=(unsigned long)p->clip_h*w;if(s->scale==OAV_FILL)bywidth=!bywidth;
 if(bywidth){p->w=p->clip_w;p->h=(long)((h*(unsigned long)p->clip_w+w/2)/w);}
 else{p->h=p->clip_h;p->w=(long)((w*(unsigned long)p->clip_h+h/2)/h);}
 p->x=(p->page_w-p->w)/2;p->y=(p->page_h-p->h)/2;return 1;
}
static void point_string(char *out,long v)
{
 unsigned long a=(unsigned long)(v<0?-v:v);
 sprintf(out,"%s%lu.%02lu",v<0?"-":"",a/100,a%100);
}
static void fail(char *e,size_t n,const char *s){oap_copy(e,n,s);}
int oav_pdf_rgb(FILE *f,unsigned long w,unsigned long h,const OAVLayout *s,
               OAVReadRow readrow,void *ctx,char *err,size_t cap)
{
    OAVPlacement p;long off[7],xref;unsigned char *row=NULL;unsigned long y;
    char content[512],numbers[10][24];int clen,i;size_t rowbytes;
    if(!f||!readrow||!oav_place(s,w,h,&p)){fail(err,cap,"Invalid dimensions or page layout");return 0;}
    if(w>SIZE_MAX/3){fail(err,cap,"Image row exceeds address range");return 0;}
    rowbytes=(size_t)w*3;row=malloc(rowbytes);if(!row){fail(err,cap,"Not enough memory for image row");return 0;}
    point_string(numbers[0],p.clip_x);point_string(numbers[1],p.clip_y);
    point_string(numbers[2],p.clip_w);point_string(numbers[3],p.clip_h);
    point_string(numbers[4],p.w);point_string(numbers[5],p.h);
    point_string(numbers[6],p.x);point_string(numbers[7],p.y);
    point_string(numbers[8],p.page_w);point_string(numbers[9],p.page_h);
    clen=snprintf(content,sizeof(content),"q\n%s %s %s %s re W n\n%s 0 0 %s %s %s cm\n/Im0 Do\nQ\n",
        numbers[0],numbers[1],numbers[2],numbers[3],numbers[4],numbers[5],numbers[6],numbers[7]);
    if(clen<0||(size_t)clen>=sizeof(content)){free(row);fail(err,cap,"Page command overflow");return 0;}
    fputs("%PDF-1.4\n% OpenView RGB export\n",f);
    off[1]=ftell(f);fputs("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",f);
    off[2]=ftell(f);fputs("2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n",f);
    off[3]=ftell(f);fprintf(f,"3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %s %s] /Resources << /XObject << /Im0 5 0 R >> >> /Contents 4 0 R >>\nendobj\n",numbers[8],numbers[9]);
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
/* ---- Find ---------------------------------------------------------------- */
static int lc1(unsigned char c)
{
 if(c>='A'&&c<='Z')return c+32;
 if(c>=0xc0&&c<=0xde&&c!=0xd7)return c+32;      /* Latin-1 capitals */
 return c;
}
static int at_word(const char *b,long len,long i,const char *w)
{
 while(*w){if(i>=len||lc1((unsigned char)b[i])!=lc1((unsigned char)*w))return 0;i++;w++;}
 return 1;
}
int oav_is_guide(const char *buf,long len)
{
 long i=0;
 while(i<len&&(buf[i]==' '||buf[i]=='\t'||buf[i]=='\r'||buf[i]=='\n'))i++;
 return at_word(buf,len,i,"@database");
}
/* A guide line that is a command (@node, @title, ...), not shown; "@{" is a link inside text. */
static int command_line(const char *b,long len,long s)
{
 return s<len&&b[s]=='@'&&!(s+1<len&&b[s+1]=='{');
}
static int match_at(const char *b,long len,long i,const char *w,long wl)
{
 long k;
 if(i+wl>len)return 0;
 for(k=0;k<wl;k++)if(lc1((unsigned char)b[i+k])!=lc1((unsigned char)w[k]))return 0;
 return 1;
}
/* Fills in the line (and for a guide the node) of the match at `at`; 0 when a
 * guide does not show that place (a command line, or outside any node). */
static int place(const char *b,long len,long at,int guide,OAVFound *f)
{
 long i,s=0,line=0,nline=0;int innode=0;
 f->node[0]=0;
 for(i=0;i<at;i++){
  if(b[i]!='\n')continue;
  if(guide){
   if(at_word(b,len,s,"@node")){
    long p=s+5,n=0;char end=' ';
    while(p<len&&(b[p]==' '||b[p]=='\t'))p++;
    if(p<len&&b[p]=='"'){end='"';p++;}
    while(p<len&&b[p]!=end&&b[p]!='\n'&&b[p]!='\r'&&!(end==' '&&b[p]=='\t')&&n<(long)sizeof(f->node)-1)f->node[n++]=b[p++];
    f->node[n]=0;innode=1;nline=0;
   }else if(at_word(b,len,s,"@endnode"))innode=0;
   else if(innode&&!command_line(b,len,s))nline++;
  }
  line++;s=i+1;
 }
 f->line=line;f->node_line=nline;
 if(guide&&(!innode||command_line(b,len,s)||at_word(b,len,s,"@node")))return 0;
 return 1;
}
int oav_find(const char *buf,long len,const char *what,long from,int guide,OAVFound *f)
{
 long wl=what?(long)strlen(what):0,i,pass;
 memset(f,0,sizeof(*f));f->at=-1;
 if(!buf||wl<=0||len<wl)return 0;
 if(from<0||from>len)from=0;
 for(pass=0;pass<2;pass++){
  long a=pass?0:from,z=pass?from:len;
  for(i=a;i<z&&i+wl<=len;i++)
   if(match_at(buf,len,i,what,wl)&&place(buf,len,i,guide,f)){f->at=i;f->wrapped=(int)pass;return 1;}
 }
 return 0;
}
