#include <exec/types.h>
#include <devices/parallel.h>
#include <proto/exec.h>
#include <stdio.h>

int main(void)
{
    struct MsgPort *p=NULL;
    struct IOExtPar *io=NULL;
    UBYTE status;
    int rc=20;

    p=CreateMsgPort();
    if(p)io=(struct IOExtPar *)CreateIORequest(p,sizeof(*io));
    if(!p||!io){puts("OpenAmigaPrint: no memory");goto done;}
    if(OpenDevice((STRPTR)"oapspool.device",0,(struct IORequest *)io,0)){
        puts("OpenAmigaPrint: cannot open oapspool.device");goto done;
    }

    io->IOPar.io_Command=PDCMD_QUERY;
    if(DoIO((struct IORequest *)io)){
        printf("OpenAmigaPrint: status query failed %ld\n",(long)io->IOPar.io_Error);
        goto closeit;
    }
    status=io->io_Status;
    printf("OpenAmigaPrint: virtual status 0x%02lx\n",(long)status);
    if(status&IOPTF_PAPEROUT){puts("FAIL: virtual PDF printer reports paper out");goto closeit;}
    if(status&IOPTF_PARBUSY){puts("FAIL: virtual PDF printer reports busy/offline");goto closeit;}
    if(!(status&IOPTF_PARSEL)){puts("FAIL: virtual PDF printer is not selected");goto closeit;}
    if(!(status&IOPTF_RWDIR)){puts("FAIL: virtual PDF printer is not in write direction");goto closeit;}
    puts("PASS: virtual PDF printer is ready and can never report paper out");
    rc=0;
closeit:
    CloseDevice((struct IORequest *)io);
done:
    if(io)DeleteIORequest((struct IORequest *)io);
    if(p)DeleteMsgPort(p);
    return rc;
}
