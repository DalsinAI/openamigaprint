#include <exec/types.h>
#include <exec/io.h>
#include <proto/exec.h>
#include <stdio.h>

static const char message[] =
    "OpenPrint first-light\n"
    "Classic printer.device to PDF to native print UI.\n"
    "If you can read this in the generated PDF, text printing works.\n"
    "\f";

int main(void)
{
    struct MsgPort *p=NULL;
    struct IOStdReq *io=NULL;
    int rc=20;
    p=CreateMsgPort();
    if(p)io=(struct IOStdReq *)CreateIORequest(p,sizeof(*io));
    if(!p||!io){puts("OpenPrint: no memory");goto done;}
    if(OpenDevice((STRPTR)"printer.device",0,(struct IORequest *)io,0)){
        puts("OpenPrint: cannot open printer.device");
        goto done;
    }
    io->io_Command=CMD_WRITE;
    io->io_Data=(APTR)message;
    io->io_Length=sizeof(message)-1;
    if(DoIO((struct IORequest *)io)){
        printf("OpenPrint: printer.device error %ld\n",(long)io->io_Error);
        goto closeit;
    }
    puts("OpenPrint: text job submitted");
    rc=0;
closeit:
    CloseDevice((struct IORequest *)io);
done:
    if(io)DeleteIORequest((struct IORequest *)io);
    if(p)DeleteMsgPort(p);
    return rc;
}
