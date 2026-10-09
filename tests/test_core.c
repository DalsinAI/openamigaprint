/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#include "oap.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(x,m) do{if(x)printf("ok - %s\n",m);else{printf("FAIL - %s\n",m);fails++;}}while(0)
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
    return fails?1:0;
}
