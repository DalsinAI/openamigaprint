#include "oap.h"
#include "oap_stack.h"
#include <stdio.h>
#include <string.h>

unsigned long __stack = 65536;

static int print_main(int argc,char **argv)
{
    OAPJobOptions o; const char *pdf;
    oap_job_defaults(&o);
    if(argc<2 || !strcmp(argv[1],"QUEUE") || !strcmp(argv[1],"queue"))return oap_run_queue_window()?0:5;
    pdf=argv[1];
    if(argc>2){strncpy(o.printer_uri,argv[2],sizeof(o.printer_uri)-1);o.printer_uri[sizeof(o.printer_uri)-1]=0;}
    return oap_run_print_dialog(pdf,&o)?0:5;
}
int main(int argc,char **argv){return oap_main_with_stack(print_main,argc,argv,65536);}
