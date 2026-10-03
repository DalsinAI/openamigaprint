#include <exec/types.h>
#include <exec/io.h>
#include <proto/exec.h>
#include <stdio.h>

int main(int argc,char **argv)
{
    struct MsgPort *p=NULL;
    struct IOStdReq *io=NULL;
    FILE *f=NULL;
    unsigned char b[4096];
    size_t n;
    int rc=20;
    if(argc<2){puts("usage: oapspooltest file.pdf");return 5;}
    f=fopen(argv[1],"rb");if(!f){puts("cannot open input");return 10;}
    p=CreateMsgPort();
    if(p)io=(struct IOStdReq *)CreateIORequest(p,sizeof(*io));
    if(!p||!io){puts("no memory");goto done;}
    if(OpenDevice((STRPTR)"oapspool.device",0,(struct IORequest *)io,0)){
        puts("cannot open oapspool.device");goto done;
    }
    while((n=fread(b,1,sizeof(b),f))>0){
        io->io_Command=CMD_WRITE;io->io_Data=b;io->io_Length=n;
        if(DoIO((struct IORequest *)io)||io->io_Actual!=n){puts("write failed");goto closeit;}
    }
    puts("spool stream sent");rc=0;
closeit:
    CloseDevice((struct IORequest *)io);
done:
    if(io)DeleteIORequest((struct IORequest *)io);
    if(p)DeleteMsgPort(p);
    fclose(f);
    return rc;
}
