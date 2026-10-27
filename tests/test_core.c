/* Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT */
#include "oap.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(x,m) do{if(x)printf("ok - %s\n",m);else{printf("FAIL - %s\n",m);fails++;}}while(0)

/* The value of attribute `name` in an IPP request (its first), or NULL; its length in *len. */
static const unsigned char *ipp_value(const unsigned char *b,size_t n,const char *name,size_t *len)
{
    size_t x=9,nl=strlen(name);
    while(x+5<=n&&b[x]!=3){
        size_t a,v;
        if(b[x]<0x10){x++;continue;}
        a=(size_t)b[x+1]<<8|b[x+2];v=(size_t)b[x+3+a]<<8|b[x+4+a];
        if(a==nl&&!memcmp(b+x+3,name,nl)){*len=v;return b+x+5+a;}
        x+=5+a+v;
    }
    return NULL;
}
static int ipp_is(const unsigned char *b,size_t n,const char *name,const char *want)
{
    size_t len;const unsigned char *v=ipp_value(b,n,name,&len);
    return v&&len==strlen(want)&&!memcmp(v,want,len);
}

/* What OpenPrint sends a printer for a program's own PostScript (27 October 2026). */
static void raw_postscript(void)
{
    static const char ps[]="%!PS-Adobe-3.0\n%%Creator: PageStream\n%%Title: (Quarterly report)\n%%EndComments\nshowpage\n";
    OAPJobOptions o;unsigned char b[2048],a[2048];size_t n=0,m=0,len;const unsigned char *v;char t[96];
    CHECK(oap_document_format((const unsigned char *)"%PDF-1.4",8)&&!strcmp(oap_document_format((const unsigned char *)"%PDF-1.4",8),OAP_FORMAT_PDF),"a PDF by its %PDF-");
    CHECK(oap_document_format((const unsigned char *)"%!PS",4)&&!strcmp(oap_document_format((const unsigned char *)"%!PS",4),OAP_FORMAT_PS),"PostScript by its %!");
    CHECK(oap_document_format((const unsigned char *)"\004%!PS",5)&&!strcmp(oap_document_format((const unsigned char *)"\004%!PS",5),OAP_FORMAT_PS),"PostScript after a driver's ^D");
    CHECK(!oap_document_format((const unsigned char *)"\033E\033&l0O",7)&&!oap_document_format((const unsigned char *)"Hello",5),"PCL and text are neither");
    CHECK(!oap_document_format((const unsigned char *)"%",1)&&!oap_document_format((const unsigned char *)"%PDF",4),"too short to tell is neither");
    CHECK(oap_ps_title((const unsigned char *)ps,sizeof(ps)-1,t,sizeof(t))&&!strcmp(t,"Quarterly report"),"%%Title: (string) without its parentheses");
    CHECK(oap_ps_title((const unsigned char *)"%!PS\r\n%%Title: My letter  \r\n",28,t,sizeof(t))&&!strcmp(t,"My letter"),"%%Title: text, CR LF, trailing spaces");
    CHECK(!oap_ps_title((const unsigned char *)"%!PS\n%%EndComments\n%%Title: (late)\n",36,t,sizeof(t))&&!t[0],"a %%Title after the header comments isn't the document's");
    CHECK(!oap_ps_title((const unsigned char *)"%!PS\n%%Title:\n",15,t,sizeof(t)),"an empty %%Title is none");
    CHECK(oap_ps_title((const unsigned char *)"%!PS\n%%Title: abcdefghij\n",26,t,5)&&!strcmp(t,"abcd"),"a long title is cut to fit");
    oap_job_defaults(&o);strcpy(o.printer_uri,"ipp://10.0.0.9:8631/ipp/print");strcpy(o.job_name,"Quarterly report");
    o.copies=3;o.duplex=OAP_DUPLEX_LONG;o.page_start=2;o.page_end=4;o.orientation=OAP_LANDSCAPE;
    CHECK(oap_ipp_build_prefix_doc(&o,OAP_FORMAT_PS,b,sizeof(b),&n)&&b[3]==2&&b[n-1]==3,"a PostScript Print-Job");
    CHECK(ipp_is(b,n,"document-format","application/postscript"),"document-format application/postscript");
    CHECK(ipp_is(b,n,"job-name","Quarterly report"),"job-name as given");
    v=ipp_value(b,n,"copies",&len);
    CHECK(v&&len==4&&v[3]==3&&!v[0]&&!v[1]&&!v[2],"copies as given");
    CHECK(!ipp_value(b,n,"media",&len)&&!ipp_value(b,n,"sides",&len)&&!ipp_value(b,n,"orientation-requested",&len)&&
          !ipp_value(b,n,"print-color-mode",&len)&&!ipp_value(b,n,"page-ranges",&len),"PostScript keeps its own paper, sides and pages");
    CHECK(oap_ipp_build_prefix(&o,a,sizeof(a),&m)&&oap_ipp_build_prefix_doc(&o,OAP_FORMAT_PDF,b,sizeof(b),&n)&&m==n&&!memcmp(a,b,n),"a PDF's request is oap_ipp_build_prefix's, as before");
    CHECK(ipp_is(b,n,"media","iso_a4_210x297mm")&&ipp_is(b,n,"sides","two-sided-long-edge")&&ipp_value(b,n,"page-ranges",&len),"a PDF still has its page settings");
    CHECK(!oap_ipp_build_prefix_doc(&o,"application/vnd.hp-PCL",b,sizeof(b),&n)&&!oap_ipp_build_prefix_doc(&o,NULL,b,sizeof(b),&n),"no request for PCL or no format");
    strcpy(o.job_name,"Caf\351 menu");
    CHECK(oap_ipp_build_prefix_doc(&o,OAP_FORMAT_PS,b,sizeof(b),&n)&&ipp_is(b,n,"job-name","Caf\303\251 menu"),"a Latin-1 job name goes as UTF-8");
    strcpy(o.job_name,"Caf\303\251 menu");
    CHECK(oap_ipp_build_prefix_doc(&o,OAP_FORMAT_PS,b,sizeof(b),&n)&&ipp_is(b,n,"job-name","Caf\303\251 menu"),"a UTF-8 job name goes as it is");
}
int main(void)
{
    OAPUri u; OAPJobOptions o; unsigned char b[2048]; size_t n=0;
    CHECK(oap_parse_ipp_uri("ipp://printer.local/ipp/print",&u)&&!strcmp(u.host,"printer.local")&&u.port==631&&!strcmp(u.path,"/ipp/print"),"parse default IPP URI");
    CHECK(oap_parse_ipp_uri("ipp://10.0.0.9:8631/printers/x",&u)&&u.port==8631&&!strcmp(u.path,"/printers/x"),"parse explicit port");
    CHECK(oap_parse_ipp_uri("ipps://printer/ipp/print",&u)&&u.secure&&u.port==631&&!strcmp(u.host,"printer")&&!strcmp(u.path,"/ipp/print"),"parse IPPS: port 631 by default");
    CHECK(oap_parse_ipp_uri("ipps://10.0.0.9:443/ipp/print",&u)&&u.secure&&u.port==443,"parse IPPS with a port");
    CHECK(oap_parse_ipp_uri("ipp://printer/ipp/print",&u)&&!u.secure,"plain IPP is not secure");
    CHECK(!oap_parse_ipp_uri("https://printer/ipp/print",&u)&&!oap_parse_ipp_uri("ippsx://printer/",&u),"refuse other schemes");
    CHECK(oap_parse_ipp_uri("ipp://10.0.0.9",&u)&&u.port==631&&!strcmp(u.path,"/ipp/print"),"no path: the IPP Everywhere resource");
    CHECK(!oap_parse_ipp_uri("ipp://10.0.0.9:63x/ipp/print",&u),"refuse a port with letters");
    CHECK(!oap_parse_ipp_uri("ipp://10.0.0.9:/ipp/print",&u)&&!oap_parse_ipp_uri("ipp://10.0.0.9:70000/",&u),"refuse an empty or too large port");
    CHECK(!oap_parse_ipp_uri("ipp://kim@10.0.0.9/ipp/print",&u),"refuse a user in the address");
    CHECK(!oap_parse_ipp_uri("ipp://[fe80::1]/ipp/print",&u),"refuse an IPv6 literal");
    CHECK(!oap_parse_ipp_uri("ipp://host/ipp print",&u)&&!oap_parse_ipp_uri("ipp:///ipp/print",&u),"refuse a space or no host");
    oap_job_defaults(&o); strcpy(o.printer_uri,"ipp://printer.local/ipp/print"); o.copies=2; o.duplex=OAP_DUPLEX_LONG; o.page_start=2; o.page_end=4;
    CHECK(oap_ipp_build_prefix(&o,b,sizeof(b),&n)&&n>100&&b[0]==1&&b[1]==1&&b[2]==0&&b[3]==2&&b[n-1]==3,"build IPP Print-Job prefix");
    CHECK(memmem(b,n,"application/pdf",15)!=NULL,"IPP advertises PDF document format");
    CHECK(oap_pdf_write_demo("build/oap-firstlight.pdf","Host core test"),"write demo PDF");
    raw_postscript();
    return fails?1:0;
}
