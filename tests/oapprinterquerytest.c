#include <exec/types.h>
#include <exec/io.h>
#include <devices/printer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>

struct PStat { UBYTE LSB; UBYTE MSB; };

static void mark(const char *path,const char *text)
{
    BPTR f=Open((STRPTR)path,MODE_NEWFILE);
    if(f){Write(f,(APTR)text,(LONG)strlen(text));Close(f);}
}

int main(void)
{
    struct MsgPort *p=NULL;
    struct IOStdReq *io=NULL;
    struct PStat st={0,0};
    int rc=20;

    mark("DH1:OAPQuery.stage1","before OpenDevice printer.device\n");
    p=CreateMsgPort();
    if(p)io=(struct IOStdReq *)CreateIORequest(p,sizeof(*io));
    if(!p||!io){puts("OpenPrint: no memory");goto done;}
    if(OpenDevice((STRPTR)"printer.device",0,(struct IORequest *)io,0)){
        mark("DH1:OAPQuery.stage_openfail","OpenDevice printer.device failed\n");
        puts("OpenPrint: cannot open printer.device");goto done;
    }
    mark("DH1:OAPQuery.stage2","after OpenDevice printer.device\n");
    io->io_Command=PRD_QUERY;
    io->io_Data=&st;
    io->io_Length=sizeof(st);
    mark("DH1:OAPQuery.stage3","before PRD_QUERY DoIO\n");
    if(DoIO((struct IORequest *)io)){
        mark("DH1:OAPQuery.stage_queryfail","PRD_QUERY failed\n");
        printf("OpenPrint: PRD_QUERY failed %ld\n",(long)io->io_Error);
        goto closeit;
    }
    mark("DH1:OAPQuery.stage4","after PRD_QUERY DoIO\n");
    printf("OpenPrint: printer.device status LSB=0x%02lx MSB=0x%02lx type=%ld\n",
           (long)st.LSB,(long)st.MSB,(long)io->io_Actual);
    if(st.LSB&0x02){puts("FAIL: printer.device reports paper out");goto closeit;}
    if(st.LSB&0x01){puts("FAIL: printer.device reports busy/offline");goto closeit;}
    if(!(st.LSB&0x04)){puts("FAIL: printer.device reports printer not selected");goto closeit;}
    puts("PASS: printer.device sees the OpenPrint port as ready");
    rc=0;
closeit:
    CloseDevice((struct IORequest *)io);
done:
    if(io)DeleteIORequest((struct IORequest *)io);
    if(p)DeleteMsgPort(p);
    return rc;
}
