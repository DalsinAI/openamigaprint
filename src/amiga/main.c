#include "oap.h"
#include <stdio.h>
#include <string.h>

int main(int argc,char **argv)
{
    OAPJobOptions o; const char *pdf;
    oap_job_defaults(&o);
    if(argc>1)pdf=argv[1]; else {pdf="T:OpenAmigaPrint-firstlight.pdf";if(!oap_pdf_write_demo(pdf,"Generated on the Amiga")){puts("OpenAmigaPrint: cannot create demo PDF");return 20;}}
    if(argc>2){strncpy(o.printer_uri,argv[2],sizeof(o.printer_uri)-1);o.printer_uri[sizeof(o.printer_uri)-1]=0;}
    return oap_run_print_dialog(pdf,&o)?0:5;
}
