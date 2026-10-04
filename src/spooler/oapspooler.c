/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
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
#include "oap_queue.h"

static BPTR spool_file;
static ULONG spool_bytes;
static UWORD sequence;
static char spool_path[80];
static char meta_path[80];
static char tail[8];
static UBYTE tail_len;

static int ensure_dir(const char *path)
{
    BPTR l=Lock((STRPTR)path,ACCESS_READ);
    if(l){UnLock(l);return 1;}
    l=CreateDir((STRPTR)path);if(!l)return 0;UnLock(l);return 1;
}
static int ensure_queue_dir(void)
{
    return ensure_dir(OAP_QUEUE_ROOT)&&ensure_dir(OAP_QUEUE_DIR);
}
static void make_path(void)
{
    static const char hex[]="0123456789ABCDEF";BPTR l;
    do {
        char *p=spool_path;const char *s=OAP_QUEUE_DIR "/" OAP_QUEUE_PREFIX;UWORD v=++sequence;
        while(*s)*p++=*s++;
        *p++=hex[(v>>12)&15];*p++=hex[(v>>8)&15];*p++=hex[(v>>4)&15];*p++=hex[v&15];
        s=".pdf";while(*s)*p++=*s++;*p=0;
        l=Lock((STRPTR)spool_path,ACCESS_READ);if(l)UnLock(l);
    } while(l);
    {char *p=meta_path;const char *s=spool_path;while(*s)*p++=*s++;p-=4;s=".job";while(*s)*p++=*s++;*p=0;}
}

static void close_spool(void){if(spool_file){Close(spool_file);spool_file=0;}}
static int ensure_spool(void)
{
    if(spool_file)return 1;
    if(!ensure_queue_dir())return 0;
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
static LONG cstrlen(const char *s){LONG n=0;while(s&&s[n])n++;return n;}
static void write_str(BPTR f,const char *s){LONG n=cstrlen(s);if(n)Write(f,(APTR)s,n);}
static void write_ulong(BPTR f,ULONG v)
{
    char b[11];int n=0,i;if(!v){Write(f,(APTR)"0",1);return;}
    while(v&&n<10){b[n++]=(char)('0'+(v%10));v/=10;}
    for(i=n-1;i>=0;i--)Write(f,(APTR)&b[i],1);
}
static void write_metadata(void)
{
    BPTR f=Open((STRPTR)meta_path,MODE_NEWFILE);
    if(!f)return;
    write_str(f,"state=" OAP_QUEUE_STATE_QUEUED "\nbytes=");write_ulong(f,spool_bytes);
    write_str(f,"\npdf=");write_str(f,spool_path);write_str(f,"\n");Close(f);
}
static void finish_job(void)
{
    if(!spool_file||!spool_bytes)return;
    close_spool();write_metadata();spool_bytes=0;tail_len=0;
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
        if(spool_file){close_spool();DeleteFile((STRPTR)spool_path);DeleteFile((STRPTR)meta_path);}
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
    ensure_queue_dir();
    for(;;){WaitPort(port);while((msg=GetMsg(port))!=NULL)handle((struct IOStdReq *)msg);}
    return 0;
}
