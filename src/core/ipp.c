/* Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT */
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

/* ipp://host[:port][/path] or ipps://host[:port][/path] (IPP over TLS).
 * The host is a name or a dotted address (no user, no IPv6 literal); the
 * port defaults to 631 for both (RFC 8010: IPPS is TLS on IPP's own
 * port) and the path to /ipp/print, the IPP Everywhere resource. One
 * parser for the query and the print job, so both always reach the same
 * resource. */
int oap_parse_ipp_uri(const char *uri,OAPUri *out)
{
    const char *host,*end,*slash,*colon,*q;
    size_t n;
    int secure;
    if(!uri||!out)return 0;
    if(!strncmp(uri,"ipp://",6))secure=0;
    else if(!strncmp(uri,"ipps://",7))secure=1;
    else return 0;
    memset(out,0,sizeof(*out));
    out->port=631;
    out->secure=secure;
    host=uri+6+secure;
    slash=strchr(host,'/');
    end=slash?slash:host+strlen(host);
    colon=memchr(host,':',(size_t)(end-host));
    n=(size_t)((colon?colon:end)-host);
    if(!n||n>=sizeof(out->host))return 0;
    for(q=host;q<host+n;q++)
        if((unsigned char)*q<=32||(unsigned char)*q>=127||*q=='@'||*q=='['||*q==']'||*q=='\\')return 0;
    memcpy(out->host,host,n);
    if(colon){
        unsigned long v=0;
        if(colon+1==end)return 0;
        for(q=colon+1;q<end;q++){
            if(*q<'0'||*q>'9'||v>6553)return 0;
            v=v*10+(unsigned long)(*q-'0');
        }
        if(!v||v>65535)return 0;
        out->port=(unsigned short)v;
    }
    if(!slash)slash="/ipp/print";
    n=strlen(slash);
    if(n>=sizeof(out->path))return 0;
    for(q=slash;*q;q++)
        if((unsigned char)*q<=32||(unsigned char)*q>=127||*q=='#')return 0;
    memcpy(out->path,slash,n+1);
    return 1;
}
/* The IPP media name for each OAP_PAPER_ size, in its order (PWG 5101.1 self-describing names). */
const char *oap_ipp_media(int paper)
{
    static const char *const media[OAP_PAPERS]={"iso_a4_210x297mm","na_letter_8.5x11in","na_legal_8.5x14in",
                                               "iso_a3_297x420mm","iso_a5_148x210mm"};
    return paper>=0&&paper<OAP_PAPERS?media[paper]:media[OAP_PAPER_A4];
}
/* job-name in UTF-8, as attributes-charset says. Amiga text is Latin-1: a name that isn't UTF-8 already is
 * taken as Latin-1 (an ASCII name is the same bytes either way). */
static size_t utf8_name(const char *s,unsigned char *out,size_t cap)
{
    const unsigned char *p=(const unsigned char *)s;size_t i=0,j,k,n=0;int utf8=1;
    while(p[i]&&utf8){
        k=p[i]<0x80?0:(p[i]&0xe0)==0xc0?1:(p[i]&0xf0)==0xe0?2:(p[i]&0xf8)==0xf0?3:4;
        if(k==4)utf8=0;
        for(j=1;utf8&&j<=k;j++)if((p[i+j]&0xc0)!=0x80)utf8=0;
        i+=k+1;
    }
    for(i=0;p[i]&&n+2<=cap;i++){
        if(utf8||p[i]<0x80)out[n++]=p[i];
        else{out[n++]=(unsigned char)(0xc0|p[i]>>6);out[n++]=(unsigned char)(0x80|(p[i]&0x3f));}
    }
    return n;
}
int oap_ipp_build_prefix(const OAPJobOptions *o,unsigned char *b,size_t c,size_t *out)
{
    return oap_ipp_build_prefix_doc(o,OAP_FORMAT_PDF,b,c,out);
}
int oap_ipp_build_prefix_doc(const OAPJobOptions *o,const char *format,unsigned char *b,size_t c,size_t *out)
{
    size_t p=0,nl; const char *media,*sides,*colour; unsigned orient; int ps;
    unsigned char range[8],name[256];
    if(!o||!b||!out||!format)return 0;
    ps=!strcmp(format,OAP_FORMAT_PS);
    if(!ps&&strcmp(format,OAP_FORMAT_PDF))return 0;
    nl=utf8_name(o->job_name,name,sizeof(name));
    media=oap_ipp_media(o->paper);
    sides=o->duplex==OAP_DUPLEX_LONG?"two-sided-long-edge":o->duplex==OAP_DUPLEX_SHORT?"two-sided-short-edge":"one-sided";
    colour=o->color==OAP_MONO?"monochrome":"color"; orient=o->orientation==OAP_LANDSCAPE?4:3;
    if(!put8(b,c,&p,1)||!put8(b,c,&p,1)||!put16(b,c,&p,0x0002)||!put32(b,c,&p,1))return 0;
    if(!put8(b,c,&p,0x01))return 0;
    if(!attr_s(b,c,&p,0x47,"attributes-charset","utf-8"))return 0;
    if(!attr_s(b,c,&p,0x48,"attributes-natural-language","en"))return 0;
    if(!attr_s(b,c,&p,0x45,"printer-uri",o->printer_uri))return 0;
    if(!attr_s(b,c,&p,0x42,"requesting-user-name","OpenPrint"))return 0;
    if(!attr(b,c,&p,0x42,"job-name",name,nl))return 0;
    if(!attr_s(b,c,&p,0x49,"document-format",format))return 0;
    if(!put8(b,c,&p,0x02))return 0;
    if(!attr_i(b,c,&p,0x21,"copies",o->copies>0?(unsigned long)o->copies:1))return 0;
    /* PostScript: its own setpagedevice says the paper, the sides and the pages; the printer's defaults do the rest */
    if(!ps){
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
    }
    if(!put8(b,c,&p,0x03))return 0;
    *out=p; return 1;
}
