/* SPDX-License-Identifier: BSD-2-Clause */
#include <exec/types.h>
#include <exec/io.h>
#include <exec/errors.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "oap_spooler.h"

static BPTR spool_file;
static ULONG spool_bytes;
static UWORD sequence;
static char spool_path[48];
static char tail[8];
static UBYTE tail_len;

static void make_path(void)
{
    static const char hex[]="0123456789ABCDEF";
    char *p=spool_path;const char *s="T:OpenAmigaPrint-job-";UWORD v=++sequence;
    while(*s)*p++=*s++;
    *p++=hex[(v>>12)&15];*p++=hex[(v>>8)&15];*p++=hex[(v>>4)&15];*p++=hex[v&15];
    s=".pdf";while(*s)*p++=*s++;*p=0;
}
static void close_spool(void){if(spool_file){Close(spool_file);spool_file=0;}}
static int ensure_spool(void)
{
    if(spool_file)return 1;
    make_path();spool_file=Open((STRPTR)spool_path,MODE_NEWFILE);spool_bytes=0;tail_len=0;
    return spool_file!=0;
}
static void update_tail(const UBYTE *p,ULONG n)
{
    ULONG i;
    for(i=0;i<n;i++){
        if(tail_len<sizeof(tail))tail[tail_len++]=(char)p[i];
        else {int j;for(j=0;j<(int)sizeof(tail)-1;j++)tail[j]=tail[j+1];tail[sizeof(tail)-1]=(char)p[i];}
    }
}
static int ends_pdf(void)
{
    static const char eof[]="%%EOF";int i,j;
    if(tail_len<5)return 0;
    for(i=0;i<=tail_len-5;i++){for(j=0;j<5&&tail[i+j]==eof[j];j++);if(j==5)return 1;}
    return 0;
}
static void launch_job(void)
{
    char cmd[96];char *d=cmd;const char *s="C:OpenAmigaPrint ";struct TagItem tags[2];
    while(*s)*d++=*s++;
    s=spool_path;
    while(*s&&d<cmd+sizeof(cmd)-1)*d++=*s++;
    *d=0;
    tags[0].ti_Tag=SYS_Asynch;tags[0].ti_Data=TRUE;
    tags[1].ti_Tag=TAG_DONE;tags[1].ti_Data=0;
    SystemTagList((STRPTR)cmd,tags);
}
static void finish_job(void)
{
    if(!spool_file||!spool_bytes)return;
    close_spool();launch_job();spool_bytes=0;tail_len=0;
}
static void handle(struct IOStdReq *io)
{
    LONG wrote;
    io->io_Error=0;io->io_Actual=0;
    switch(io->io_Command){
    case CMD_WRITE:
        if(!io->io_Data||!io->io_Length||!ensure_spool()){io->io_Error=IOERR_OPENFAIL;break;}
        wrote=Write(spool_file,io->io_Data,io->io_Length);
        if(wrote<0){io->io_Error=IOERR_ABORTED;break;}
        io->io_Actual=wrote;spool_bytes+=wrote;update_tail((const UBYTE *)io->io_Data,wrote);
        if(ends_pdf())finish_job();
        break;
    case CMD_RESET:
        if(spool_file){close_spool();DeleteFile((STRPTR)spool_path);}
        spool_bytes=0;tail_len=0;break;
    case CMD_FLUSH:
    case CMD_UPDATE:
        break;
    default:
        io->io_Error=IOERR_NOCMD;break;
    }
    ReplyMsg(&io->io_Message);
}
int main(void)
{
    struct MsgPort *port;struct Message *msg;struct Process *me=(struct Process *)FindTask(NULL);
    Forbid();
    if(FindPort((STRPTR)OAP_SPOOLER_PORT)){Permit();return 5;}
    Permit();
    port=CreateMsgPort();if(!port)return 20;
    port->mp_Node.ln_Name=(char *)OAP_SPOOLER_PORT;
    AddPort(port);
    if(me)me->pr_WindowPtr=(APTR)-1;
    for(;;){
        WaitPort(port);
        while((msg=GetMsg(port))!=NULL)handle((struct IOStdReq *)msg);
    }
    return 0;
}
