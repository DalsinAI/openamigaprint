#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <devices/prtbase.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <string.h>

extern struct ExecBase *SysBase;

static void mark(const char *name,const char *text)
{
    char path[80]="DH1:"; BPTR f;
    strncat(path,name,sizeof(path)-5);
    f=Open((STRPTR)path,MODE_NEWFILE);
    if(f){Write(f,(APTR)text,(LONG)strlen(text));Close(f);}
}
static char hx(unsigned v){v&=15;return v<10?'0'+v:'a'+v-10;}
static void markhex(const char *name,const char *prefix,LONG v)
{
    char b[80]; int n=0,i; ULONG x=(ULONG)v;
    while(prefix[n]&&n<60){b[n]=prefix[n];n++;}
    for(i=7;i>=0;i--)b[n++]=hx(x>>(i*4));
    b[n++]='\n';b[n]=0;mark(name,b);
}
int main(void)
{
    BPTR seg=0;
    struct PrinterSegment *ps;
    struct PrinterExtendedData *ped;
    struct PrinterData *pd;
    LONG initrc, rendrc;
    ULONG sz=sizeof(struct PrinterData);

    mark("OAPCb.00-start","callback probe started\n");
    seg=LoadSeg((STRPTR)"DEVS:Printers/OpenAmigaPrint");
    if(!seg){mark("OAPCb.01-load","FAIL LoadSeg\n");goto done;}
    mark("OAPCb.01-load","PASS LoadSeg\n");
    ps=(struct PrinterSegment *)BADDR(seg);ped=&ps->ps_PED;
    pd=(struct PrinterData *)AllocMem(sz,MEMF_ANY|MEMF_CLEAR);
    if(!pd){mark("OAPCb.02-alloc","FAIL PrinterData allocation\n");goto done;}
    pd->pd_Device.dd_ExecBase=SysBase;
    initrc=((LONG (*)(struct PrinterData *))ped->ped_Init)(pd);
    markhex("OAPCb.03-initrc","ped_Init D0=0x",initrc);
    rendrc=ped->ped_Render(0,0,0,5,0);
    markhex("OAPCb.04-render5","ped_Render PREINIT=0x",rendrc);
    if(ped->ped_Expunge)ped->ped_Expunge();
    FreeMem(pd,sz);
done:
    if(seg)UnLoadSeg(seg);
    mark("OAPCb.99-done","callback probe completed\n");
    return 0;
}
