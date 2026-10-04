#include "oap.h"
#include <string.h>
#include <stdlib.h>

static int put8(unsigned char *b,size_t c,size_t *p,unsigned v){if(*p>=c)return 0;b[(*p)++]=(unsigned char)v;return 1;}
static int put16(unsigned char*b,size_t c,size_t*p,unsigned v){return put8(b,c,p,v>>8)&&put8(b,c,p,v);}
static int put32(unsigned char*b,size_t c,size_t*p,unsigned long v){return put8(b,c,p,v>>24)&&put8(b,c,p,v>>16)&&put8(b,c,p,v>>8)&&put8(b,c,p,v);}
static int putn(unsigned char*b,size_t c,size_t*p,const void*s,size_t n){if(*p+n>c)return 0;memcpy(b+*p,s,n);*p+=n;return 1;}
static int attr(unsigned char*b,size_t c,size_t*p,unsigned tag,const char*n,const void*v,size_t vl){size_t nl=strlen(n);return put8(b,c,p,tag)&&put16(b,c,p,nl)&&putn(b,c,p,n,nl)&&put16(b,c,p,vl)&&putn(b,c,p,v,vl);}
static int attr_s(unsigned char*b,size_t c,size_t*p,unsigned tag,const char*n,const char*v){return attr(b,c,p,tag,n,v,strlen(v));}
static int attr_i(unsigned char*b,size_t c,size_t*p,unsigned tag,const char*n,unsigned long v){unsigned char q[4];q[0]=v>>24;q[1]=v>>16;q[2]=v>>8;q[3]=v;return attr(b,c,p,tag,n,q,4);}

int oap_parse_ipp_uri(const char *uri,OAPUri *out)
{
    const char *p,*slash,*colon; size_t hn;
    if(!uri||!out||strncmp(uri,"ipp://",6)!=0)return 0;
    memset(out,0,sizeof(*out)); out->port=631; p=uri+6; slash=strchr(p,'/');
    if(!slash)slash=p+strlen(p);
    colon=NULL;
    { const char *q; for(q=p;q<slash;q++) if(*q==':') colon=q; }
    hn=(size_t)((colon?colon:slash)-p); if(!hn||hn>=sizeof(out->host))return 0;
    memcpy(out->host,p,hn); out->host[hn]=0;
    if(colon){ long v=strtol(colon+1,NULL,10); if(v<1||v>65535)return 0; out->port=(unsigned short)v; }
    if(*slash) { if(strlen(slash)>=sizeof(out->path))return 0; strcpy(out->path,slash); }
    else strcpy(out->path,"/ipp/print");
    return 1;
}
int oap_ipp_build_prefix(const OAPJobOptions *o,unsigned char *b,size_t c,size_t *out)
{
    size_t p=0; const char *media,*sides,*colour; unsigned orient;
    unsigned char range[8];
    if(!o||!b||!out)return 0;
    media=o->paper==OAP_PAPER_LETTER?"na_letter_8.5x11in":"iso_a4_210x297mm";
    sides=o->duplex==OAP_DUPLEX_LONG?"two-sided-long-edge":o->duplex==OAP_DUPLEX_SHORT?"two-sided-short-edge":"one-sided";
    colour=o->color==OAP_MONO?"monochrome":"color"; orient=o->orientation==OAP_LANDSCAPE?4:3;
    if(!put8(b,c,&p,1)||!put8(b,c,&p,1)||!put16(b,c,&p,0x0002)||!put32(b,c,&p,1))return 0;
    if(!put8(b,c,&p,0x01))return 0;
    if(!attr_s(b,c,&p,0x47,"attributes-charset","utf-8"))return 0;
    if(!attr_s(b,c,&p,0x48,"attributes-natural-language","en"))return 0;
    if(!attr_s(b,c,&p,0x45,"printer-uri",o->printer_uri))return 0;
    if(!attr_s(b,c,&p,0x42,"requesting-user-name","OpenPrint"))return 0;
    if(!attr_s(b,c,&p,0x42,"job-name",o->job_name))return 0;
    if(!attr_s(b,c,&p,0x49,"document-format","application/pdf"))return 0;
    if(!put8(b,c,&p,0x02))return 0;
    if(!attr_i(b,c,&p,0x21,"copies",o->copies>0?(unsigned long)o->copies:1))return 0;
    if(!attr_s(b,c,&p,0x44,"media",media))return 0;
    if(!attr_i(b,c,&p,0x23,"orientation-requested",orient))return 0;
    if(!attr_s(b,c,&p,0x44,"print-color-mode",colour))return 0;
    if(!attr_s(b,c,&p,0x44,"sides",sides))return 0;
    if(o->page_start>0 && o->page_end>=o->page_start){
        unsigned long a=o->page_start,z=o->page_end;
        range[0]=a>>24;range[1]=a>>16;range[2]=a>>8;range[3]=a;
        range[4]=z>>24;range[5]=z>>16;range[6]=z>>8;range[7]=z;
        if(!attr(b,c,&p,0x33,"page-ranges",range,8))return 0;
    }
    if(!put8(b,c,&p,0x03))return 0;
    *out=p; return 1;
}
