#include <exec/types.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <devices/parallel.h>
#include <devices/printer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include "oap_spooler.h"

static void mark(const char *name,const char *text)
{
    char path[64]="DH1:";
    BPTR f;
    strncat(path,name,sizeof(path)-5);
    f=Open((STRPTR)path,MODE_NEWFILE);
    if(f){Write(f,(APTR)text,(LONG)strlen(text));Close(f);}
}

int main(void)
{
    struct MsgPort *spooler;
    struct MsgPort *p=NULL;
    struct IOExtPar *par=NULL;
    struct IOStdReq *prt=NULL;
    char msg[96];

    mark("OAPField.00-start","field test started\n");

    Forbid(); spooler=FindPort((STRPTR)OAP_SPOOLER_PORT); Permit();
    if(!spooler){mark("OAPField.01-spooler","FAIL spooler port absent\n");return 10;}
    mark("OAPField.01-spooler","PASS spooler port present\n");

    p=CreateMsgPort();
    if(!p){mark("OAPField.02-direct","FAIL no msgport\n");return 20;}
    par=(struct IOExtPar *)CreateIORequest(p,sizeof(*par));
    if(!par){mark("OAPField.02-direct","FAIL no IOExtPar\n");DeleteMsgPort(p);return 20;}
    if(OpenDevice((STRPTR)"oapspool.device",0,(struct IORequest *)par,0)){
        mark("OAPField.02-direct","FAIL open oapspool.device\n");goto direct_done;
    }
    mark("OAPField.02-direct-open","PASS opened oapspool.device\n");
    par->IOPar.io_Command=PDCMD_QUERY;
    if(DoIO((struct IORequest *)par)){
        mark("OAPField.03-direct-query","FAIL PDCMD_QUERY\n");
    } else {
        msg[0]=0;
        if(par->io_Status==(IOPTF_PARSEL|IOPTF_RWDIR))
            strcpy(msg,"PASS direct status 0x0c\n");
        else if(par->io_Status&IOPTF_PAPEROUT)
            strcpy(msg,"FAIL direct status has PAPEROUT\n");
        else
            strcpy(msg,"FAIL direct status unexpected\n");
        mark("OAPField.03-direct-query",msg);
    }
    CloseDevice((struct IORequest *)par);
direct_done:
    DeleteIORequest((struct IORequest *)par); par=NULL;
    DeleteMsgPort(p); p=NULL;

    mark("OAPField.04-before-printer-open","about to OpenDevice printer.device\n");
    p=CreateMsgPort();
    if(!p){mark("OAPField.05-printer-open","FAIL no msgport\n");return 20;}
    prt=(struct IOStdReq *)CreateIORequest(p,sizeof(*prt));
    if(!prt){mark("OAPField.05-printer-open","FAIL no IO request\n");DeleteMsgPort(p);return 20;}
    {
        LONG open_rc=(LONG)OpenDevice((STRPTR)"printer.device",0,(struct IORequest *)prt,0);
        if(open_rc){
            static const char hx[]="0123456789abcdef";
            char e[]="FAIL OpenDevice rc=0x00 io_Error=0x00\n";
            UBYTE r=(UBYTE)open_rc, ie=(UBYTE)prt->io_Error;
            e[21]=hx[(r>>4)&15]; e[22]=hx[r&15];
            e[35]=hx[(ie>>4)&15]; e[36]=hx[ie&15];
            mark("OAPField.05-printer-open",e);goto printer_done;
        }
    }
    mark("OAPField.05-printer-open","PASS opened printer.device\n");
    mark("OAPField.06-before-prd-query","about to PRD_QUERY\n");
    prt->io_Command=PRD_QUERY;
    prt->io_Data=msg;
    prt->io_Length=2;
    if(DoIO((struct IORequest *)prt))
        mark("OAPField.07-prd-query","FAIL PRD_QUERY\n");
    else
        mark("OAPField.07-prd-query","PASS PRD_QUERY returned\n");
    CloseDevice((struct IORequest *)prt);
printer_done:
    DeleteIORequest((struct IORequest *)prt);
    DeleteMsgPort(p);
    mark("OAPField.99-done","field test completed\n");
    return 0;
}
