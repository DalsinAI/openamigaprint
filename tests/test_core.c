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
    CHECK(!oap_parse_ipp_uri("ipps://printer/ipp/print",&u),"IPPS deliberately deferred");
    oap_job_defaults(&o); strcpy(o.printer_uri,"ipp://printer.local/ipp/print"); o.copies=2; o.duplex=OAP_DUPLEX_LONG; o.page_start=2; o.page_end=4;
    CHECK(oap_ipp_build_prefix(&o,b,sizeof(b),&n)&&n>100&&b[0]==1&&b[1]==1&&b[2]==0&&b[3]==2&&b[n-1]==3,"build IPP Print-Job prefix");
    CHECK(memmem(b,n,"application/pdf",15)!=NULL,"IPP advertises PDF document format");
    CHECK(oap_pdf_write_demo("build/oap-firstlight.pdf","Host core test"),"write demo PDF");
    return fails?1:0;
}
