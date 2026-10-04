/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/devices.h>
#include <exec/errors.h>
#include <exec/execbase.h>
#include <exec/io.h>
#include <dos/dos.h>
#include <devices/parallel.h>
#include <proto/exec.h>
#include "oap_spooler.h"
#include "oap_device.h"

#define REG(r,decl) register decl __asm(#r)
#define OAP_VERSION 1

struct OAPSpoolBase {
    struct Library lib;
    UWORD pad;
    struct Unit unit;
    BPTR seglist;
    struct OAPDeviceStats stats;
};
struct ExecBase *SysBase;
int start(void){return -1;}
static const char dev_name[]="oapspool.device";
static const char dev_id[]="oapspool.device 0.1 (3.10.2026) OpenAmigaPrint\r\n";
static const char ver[] __attribute__((used))="$VER: oapspool.device 0.1 (3.10.2026) OpenAmigaPrint";

static BPTR dev_expunge(REG(a6,struct OAPSpoolBase *b))
{
    BPTR seg;
    if(b->lib.lib_OpenCnt){b->lib.lib_Flags|=LIBF_DELEXP;return 0;}
    seg=b->seglist;Remove(&b->lib.lib_Node);
    FreeMem((UBYTE *)b-b->lib.lib_NegSize,b->lib.lib_NegSize+b->lib.lib_PosSize);
    return seg;
}
static LONG dev_open(REG(a1,struct IORequest *io),REG(d0,ULONG unit),REG(d1,ULONG flags),REG(a6,struct OAPSpoolBase *b))
{
    b->stats.open_calls++;
    b->stats.last_open_unit=unit;
    b->stats.last_open_flags=flags;
    if(unit){io->io_Error=IOERR_OPENFAIL;return IOERR_OPENFAIL;}
    b->lib.lib_OpenCnt++;b->lib.lib_Flags&=~LIBF_DELEXP;
    b->unit.unit_OpenCnt++;
    io->io_Device=(struct Device *)b;io->io_Unit=&b->unit;io->io_Error=0;
    io->io_Message.mn_Node.ln_Type=NT_REPLYMSG;return 0;
}
static BPTR dev_close(REG(a1,struct IORequest *io),REG(a6,struct OAPSpoolBase *b))
{
    io->io_Device=(struct Device *)-1;io->io_Unit=(struct Unit *)-1;
    b->stats.close_calls++;
    if(b->unit.unit_OpenCnt)--b->unit.unit_OpenCnt;
    if(b->lib.lib_OpenCnt)--b->lib.lib_OpenCnt;
    if(!b->lib.lib_OpenCnt&&(b->lib.lib_Flags&LIBF_DELEXP))return dev_expunge(b);
    return 0;
}
static ULONG dev_null(void){return 0;}
static void reply(struct IOStdReq *io){if(!(io->io_Flags&IOF_QUICK))ReplyMsg(&io->io_Message);}

static int forward_to_spooler(struct IOStdReq *io)
{
    struct MsgPort *p;
    Forbid();
    p=FindPort((STRPTR)OAP_SPOOLER_PORT);
    if(p){
        io->io_Flags&=~IOF_QUICK;
        PutMsg(p,&io->io_Message);
    }
    Permit();
    return p!=NULL;
}
static void dev_beginio(REG(a1,struct IORequest *raw),REG(a6,struct OAPSpoolBase *b))
{
    struct IOStdReq *io=(struct IOStdReq *)raw;
    (void)b;
    io->io_Message.mn_Node.ln_Type=NT_MESSAGE;io->io_Error=0;io->io_Actual=0;
    switch(io->io_Command){
    case CMD_WRITE:
        b->stats.write_calls++;
        if(forward_to_spooler(io))return;
        io->io_Error=IOERR_OPENFAIL;
        break;
    case CMD_RESET:
    case CMD_FLUSH:
    case CMD_UPDATE:
        if(forward_to_spooler(io))return;
        io->io_Error=IOERR_OPENFAIL;
        break;
    case PDCMD_QUERY:
        b->stats.query_calls++;
        io->io_Actual=(ULONG)(IOPTF_PARSEL|IOPTF_RWDIR);

        if(raw->io_Message.mn_Length >= sizeof(struct IOExtPar))
            ((struct IOExtPar *)raw)->io_Status=(UBYTE)(IOPTF_PARSEL|IOPTF_RWDIR);
        break;
    case OAPCMD_GETSTATS:
        if(!io->io_Data || io->io_Length < sizeof(struct OAPDeviceStats)){
            io->io_Error=IOERR_BADLENGTH;
            break;
        }
        CopyMem(&b->stats,io->io_Data,sizeof(struct OAPDeviceStats));
        io->io_Actual=sizeof(struct OAPDeviceStats);
        break;
    case PDCMD_SETPARAMS:
    case CMD_CLEAR:
    case CMD_STOP:
    case CMD_START:
        break;
    case CMD_READ:
        io->io_Actual=0;break;
    default:
        io->io_Error=IOERR_NOCMD;break;
    }
    reply(io);
}
static LONG dev_abortio(REG(a1,struct IORequest *io),REG(a6,struct OAPSpoolBase *b))
{(void)io;(void)b;return IOERR_NOCMD;}
static struct OAPSpoolBase *dev_init(REG(d0,struct OAPSpoolBase *b),REG(a0,BPTR seglist),REG(a6,struct ExecBase *sys))
{
    SysBase=sys;b->seglist=seglist;
    b->lib.lib_Node.ln_Type=NT_DEVICE;b->lib.lib_Node.ln_Name=(char *)dev_name;
    b->unit.unit_MsgPort.mp_Node.ln_Type=NT_MSGPORT;
    b->unit.unit_MsgPort.mp_Flags=PA_IGNORE;

    b->unit.unit_MsgPort.mp_MsgList.lh_Head=(struct Node *)&b->unit.unit_MsgPort.mp_MsgList.lh_Tail;
    b->unit.unit_MsgPort.mp_MsgList.lh_Tail=NULL;
    b->unit.unit_MsgPort.mp_MsgList.lh_TailPred=(struct Node *)&b->unit.unit_MsgPort.mp_MsgList.lh_Head;
    b->lib.lib_Flags=LIBF_SUMUSED|LIBF_CHANGED;
    b->lib.lib_Version=OAP_VERSION;b->lib.lib_Revision=0;b->lib.lib_IdString=(APTR)dev_id;
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
