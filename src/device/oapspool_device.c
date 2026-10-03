/* SPDX-License-Identifier: BSD-2-Clause */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/devices.h>
#include <exec/errors.h>
#include <exec/execbase.h>
#include <exec/io.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>

#define REG(r,decl) register decl __asm(#r)
#define OAP_VERSION 1

struct OAPSpoolBase {
    struct Library lib;
    UWORD pad;
    BPTR seglist;
    BPTR file;
    ULONG bytes;
    UWORD sequence;
    char path[48];
    char tail[8];
    UBYTE tail_len;
};
struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
int start(void){return -1;}
static const char dev_name[]="oapspool.device";
static const char dev_id[]="oapspool.device 0.1 (3.10.2026) OpenAmigaPrint\r\n";
static const char ver[] __attribute__((used))="$VER: oapspool.device 0.1 (3.10.2026) OpenAmigaPrint";

static void make_path(struct OAPSpoolBase *b)
{
    static const char hex[]="0123456789ABCDEF";
    char *p=b->path; const char *s="T:OpenAmigaPrint-job-"; UWORD v=++b->sequence;
    while(*s)*p++=*s++;
    *p++=hex[(v>>12)&15];*p++=hex[(v>>8)&15];*p++=hex[(v>>4)&15];*p++=hex[v&15];
    s=".pdf";while(*s)*p++=*s++;*p=0;
}
static void close_spool(struct OAPSpoolBase *b)
{
    if(b->file){Close(b->file);b->file=0;}
}
static int ends_pdf(struct OAPSpoolBase *b)
{
    static const char eof[]="%%EOF"; int i,j;
    if(b->tail_len<5)return 0;
    for(i=0;i<=b->tail_len-5;i++){for(j=0;j<5&&b->tail[i+j]==eof[j];j++);if(j==5)return 1;}
    return 0;
}
static void update_tail(struct OAPSpoolBase *b,const UBYTE *p,ULONG n)
{
    ULONG i;
    for(i=0;i<n;i++){
        if(b->tail_len<sizeof(b->tail)) b->tail[b->tail_len++]=(char)p[i];
        else { int j; for(j=0;j<(int)sizeof(b->tail)-1;j++)b->tail[j]=b->tail[j+1]; b->tail[sizeof(b->tail)-1]=(char)p[i]; }
    }
}
static void launch_job(struct OAPSpoolBase *b)
{
    char cmd[96]; char *d=cmd; const char *s="C:OpenAmigaPrint "; struct TagItem tags[2];
    while(*s)*d++=*s++;
    s=b->path;
    while(*s&&d<cmd+sizeof(cmd)-1)*d++=*s++;
    *d=0;
    tags[0].ti_Tag=SYS_Asynch;tags[0].ti_Data=TRUE;
    tags[1].ti_Tag=TAG_DONE;tags[1].ti_Data=0;
    SystemTagList((STRPTR)cmd,tags);
}
static int ensure_spool(struct OAPSpoolBase *b)
{
    if(b->file)return 1;
    make_path(b);b->file=Open((STRPTR)b->path,MODE_NEWFILE);b->bytes=0;b->tail_len=0;
    return b->file!=0;
}
static void finish_job(struct OAPSpoolBase *b)
{
    if(!b->file||!b->bytes)return;
    close_spool(b);launch_job(b);b->bytes=0;b->tail_len=0;
}
static BPTR dev_expunge(REG(a6,struct OAPSpoolBase *b))
{
    BPTR seg;
    if(b->lib.lib_OpenCnt){b->lib.lib_Flags|=LIBF_DELEXP;return 0;}
    close_spool(b);
    if(DOSBase){CloseLibrary((struct Library *)DOSBase);DOSBase=0;}
    seg=b->seglist;Remove(&b->lib.lib_Node);
    FreeMem((UBYTE *)b-b->lib.lib_NegSize,b->lib.lib_NegSize+b->lib.lib_PosSize);
    return seg;
}
static LONG dev_open(REG(a1,struct IORequest *io),REG(d0,ULONG unit),REG(d1,ULONG flags),REG(a6,struct OAPSpoolBase *b))
{
    (void)flags;
    if(unit){io->io_Error=IOERR_OPENFAIL;return IOERR_OPENFAIL;}
    b->lib.lib_OpenCnt++;b->lib.lib_Flags&=~LIBF_DELEXP;
    io->io_Device=(struct Device *)b;io->io_Unit=NULL;io->io_Error=0;
    io->io_Message.mn_Node.ln_Type=NT_REPLYMSG;return 0;
}
static BPTR dev_close(REG(a1,struct IORequest *io),REG(a6,struct OAPSpoolBase *b))
{
    if(b->file&&b->bytes)finish_job(b);
    io->io_Device=(struct Device *)-1;io->io_Unit=(struct Unit *)-1;
    if(b->lib.lib_OpenCnt)--b->lib.lib_OpenCnt;
    if(!b->lib.lib_OpenCnt&&(b->lib.lib_Flags&LIBF_DELEXP))return dev_expunge(b);
    return 0;
}
static ULONG dev_null(void){return 0;}
static void reply(struct IOStdReq *io){if(!(io->io_Flags&IOF_QUICK))ReplyMsg(&io->io_Message);}
static void dev_beginio(REG(a1,struct IORequest *raw),REG(a6,struct OAPSpoolBase *b))
{
    struct IOStdReq *io=(struct IOStdReq *)raw;LONG wrote;
    io->io_Message.mn_Node.ln_Type=NT_MESSAGE;io->io_Error=0;io->io_Actual=0;
    switch(io->io_Command){
    case CMD_WRITE:
        if(!io->io_Data||!io->io_Length||!ensure_spool(b)){io->io_Error=IOERR_OPENFAIL;break;}
        wrote=Write(b->file,io->io_Data,io->io_Length);
        if(wrote<0){io->io_Error=IOERR_ABORTED;break;}
        io->io_Actual=wrote;b->bytes+=wrote;update_tail(b,(const UBYTE *)io->io_Data,wrote);
        if(ends_pdf(b))finish_job(b);
        break;
    case CMD_FLUSH: break;
    case CMD_RESET:
        if(b->file){close_spool(b);DeleteFile((STRPTR)b->path);}
        b->bytes=0;b->tail_len=0;break;
    case CMD_READ: io->io_Actual=0;break;
    default: io->io_Error=IOERR_NOCMD;break;
    }
    reply(io);
}
static LONG dev_abortio(REG(a1,struct IORequest *io),REG(a6,struct OAPSpoolBase *b))
{(void)io;(void)b;return IOERR_NOCMD;}
static struct OAPSpoolBase *dev_init(REG(d0,struct OAPSpoolBase *b),REG(a0,BPTR seglist),REG(a6,struct ExecBase *sys))
{
    SysBase=sys;b->seglist=seglist;
    b->lib.lib_Node.ln_Type=NT_DEVICE;b->lib.lib_Node.ln_Name=(char *)dev_name;
    b->lib.lib_Flags=LIBF_SUMUSED|LIBF_CHANGED;
    b->lib.lib_Version=OAP_VERSION;b->lib.lib_Revision=0;b->lib.lib_IdString=(APTR)dev_id;
    DOSBase=(struct DosLibrary *)OpenLibrary((STRPTR)"dos.library",37);
    if(!DOSBase)return NULL;
    return b;
}
static const APTR vectors[]={
    (APTR)dev_open,(APTR)dev_close,(APTR)dev_expunge,(APTR)dev_null,
    (APTR)dev_beginio,(APTR)dev_abortio,(APTR)-1
};
static const struct {ULONG size;const APTR *vectors;APTR data;APTR init;} init_table={
    sizeof(struct OAPSpoolBase),vectors,NULL,(APTR)dev_init
};
const struct Resident romtag={
    RTC_MATCHWORD,(struct Resident *)&romtag,(APTR)(&romtag+1),
    RTF_AUTOINIT,OAP_VERSION,NT_DEVICE,0,
    (char *)dev_name,(char *)dev_id,(APTR)&init_table
};
