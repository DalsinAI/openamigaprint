/* Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT */
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
#include "oap_device.h"

static BPTR spool_file;
static ULONG spool_bytes;
static UWORD sequence;
static char spool_path[80];
static char meta_path[80];
static char tail[8];
static UBYTE tail_len;
static UBYTE head[16];                  /* the job's first bytes: what it is */
static UBYTE head_len;

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
/* path's ".xxx" (from its last dot) replaced by ext */
static void with_ext(char *path,const char *ext)
{
    char *dot=0,*p;
    for(p=path;*p;p++)if(*p=='.')dot=p;
    if(!dot)dot=p;
    while(*ext)*dot++=*ext++;
    *dot=0;
}
static int exists(const char *path)
{
    BPTR l=Lock((STRPTR)path,ACCESS_READ);if(l)UnLock(l);return l!=0;
}
/* a number no job in the queue has: neither its .pdf nor its .job (a raw job keeps .ps, .pcl or .prn) */
static void make_path(void)
{
    static const char hex[]="0123456789ABCDEF";
    int taken;
    do {
        char *p=spool_path;const char *s=OAP_QUEUE_DIR "/" OAP_QUEUE_PREFIX;UWORD v=++sequence;
        while(*s)*p++=*s++;
        *p++=hex[(v>>12)&15];*p++=hex[(v>>8)&15];*p++=hex[(v>>4)&15];*p++=hex[v&15];
        s=".pdf";while(*s)*p++=*s++;*p=0;
        {char *q=meta_path;const char *t=spool_path;while((*q++=*t++)!=0);}
        with_ext(meta_path,".job");
        taken=exists(spool_path)||exists(meta_path);
    } while(taken);
}

static void close_spool(void){if(spool_file){Close(spool_file);spool_file=0;}}
static int ensure_spool(void)
{
    if(spool_file)return 1;
    if(!ensure_queue_dir())return 0;
    make_path();spool_file=Open((STRPTR)spool_path,MODE_NEWFILE);spool_bytes=0;tail_len=0;head_len=0;
    return spool_file!=0;
}
static void update_tail(const UBYTE *p,ULONG n)
{
    ULONG i;
    for(i=0;i<n&&head_len<sizeof(head);i++)head[head_len++]=p[i];
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
static int head_is(const char *s)
{
    UBYTE i;
    for(i=0;s[i];i++)if(i>=head_len||head[i]!=(UBYTE)s[i])return 0;
    return 1;
}
static int is_pdf(void){return head_is("%PDF");}
/* What a job is, from its first bytes: a PDF (OpenPrint's driver), PostScript, PCL (or PJL), or raw printer
 * data (as oap_document_format, src/core/job.c). PDFs and PostScript are printed from the queue (PostScript to
 * printers that say they take it); PCL and other data are kept, not sent. */
static void kind(const char **ext,const char **format,const char **title)
{
    if(is_pdf()){*ext=".pdf";*format="application/pdf";*title=0;}
    else if(head_is("%!")||head_is("\004%!")){*ext=".ps";*format="application/postscript";*title="Raw PostScript";}
    else if(head_is("\033%-12345X")||head_is("\033E")){*ext=".pcl";*format="application/vnd.hp-PCL";*title="Raw PCL";}
    else {*ext=".prn";*format="application/octet-stream";*title="Raw printer data";}
}
static void write_metadata(const char *state,const char *format,const char *title,const char *message)
{
    BPTR f=Open((STRPTR)meta_path,MODE_NEWFILE);
    if(!f)return;
    write_str(f,"state=");write_str(f,state);write_str(f,"\nbytes=");write_ulong(f,spool_bytes);
    write_str(f,"\npdf=");write_str(f,spool_path);
    write_str(f,"\nformat=");write_str(f,format);
    if(title){write_str(f,"\ntitle=");write_str(f,title);}
    if(message){write_str(f,"\nmessage=");write_str(f,message);}
    write_str(f,"\n");Close(f);
}
/* The job ends: a PDF at its %%EOF; anything else when oapspool.device's last opener closes (OAPCMD_ENDJOB). A
 * PDF that ends there had no %%EOF: its program stopped it (a cancelled dump), so it is marked failed, and no
 * longer runs into the next job's PDF. */
static void finish_job(void)
{
    const char *ext,*format,*title,*state=OAP_QUEUE_STATE_QUEUED,*message=0;
    if(!spool_file)return;
    close_spool();
    if(!spool_bytes){DeleteFile((STRPTR)spool_path);tail_len=head_len=0;return;}
    kind(&ext,&format,&title);
    if(is_pdf()&&!ends_pdf()){state="error";message="The program stopped printing before the end";}
    if(!is_pdf()){
        char raw[80];
        {char *q=raw;const char *t=spool_path;while((*q++=*t++)!=0);}
        with_ext(raw,ext);
        if(Rename((STRPTR)spool_path,(STRPTR)raw)){char *q=spool_path;const char *t=raw;while((*q++=*t++)!=0);}
    }
    write_metadata(state,format,title,message);spool_bytes=0;tail_len=0;head_len=0;
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
        if(is_pdf()&&ends_pdf())finish_job();     /* PostScript ends its own %%EOF: not the end of the job */
        break;
    case OAPCMD_ENDJOB:
        finish_job();
        FreeMem(io,sizeof(struct IOStdReq));       /* sent with no reply port: ours to free */
        return;
    case CMD_RESET:
        if(spool_file){close_spool();DeleteFile((STRPTR)spool_path);DeleteFile((STRPTR)meta_path);}
        spool_bytes=0;tail_len=0;head_len=0;break;
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
    /* Ctrl-C ends it (12 Oct 2026): OpenBench asks the programs it started from SYS:WBStartup to quit that way
     * before it quits, as their code is its to unload, and a spooler that never ended kept OpenBench running.
     * The port goes first, under Forbid as oapspool.device finds it, so no request comes after the last one is
     * answered. A job not yet ended keeps its .pdf but gets no .job, so it isn't queued half written. */
    for(;;){
        ULONG sigs=Wait((1UL<<port->mp_SigBit)|SIGBREAKF_CTRL_C);
        while((msg=GetMsg(port))!=NULL)handle((struct IOStdReq *)msg);
        if(sigs&SIGBREAKF_CTRL_C)break;
    }
    Forbid();
    RemPort(port);
    Permit();
    while((msg=GetMsg(port))!=NULL)handle((struct IOStdReq *)msg);
    close_spool();
    DeleteMsgPort(port);
    return 0;
}
