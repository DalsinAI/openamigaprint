#include <exec/types.h>
#include <exec/io.h>
#include <devices/timer.h>
#include <devices/prtbase.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

static void mark(const char *name,const char *text)
{
    char path[80]="DH1:"; BPTR f;
    strncat(path,name,sizeof(path)-5);
    f=Open((STRPTR)path,MODE_NEWFILE);
    if(f){Write(f,(APTR)text,(LONG)strlen(text));Close(f);}
}

int main(void)
{
    struct MsgPort *p=NULL;
    struct timerequest *tr=NULL;
    BPTR l=0,seg=0;
    struct PrinterSegment *ps;
    struct PrinterExtendedData *ped;
    char out[160];

    mark("OAPPre.00-start","preflight started\n");

    p=CreateMsgPort();
    if(p)tr=(struct timerequest *)CreateIORequest(p,sizeof(*tr));
    if(!p||!tr){mark("OAPPre.01-timer","FAIL timer allocation\n");goto timer_done;}
    if(OpenDevice((STRPTR)TIMERNAME,UNIT_VBLANK,(struct IORequest *)tr,0))
        mark("OAPPre.01-timer","FAIL timer.device unit 1 open\n");
    else {
        mark("OAPPre.01-timer","PASS timer.device unit 1 open\n");
        CloseDevice((struct IORequest *)tr);
    }
timer_done:
    if(tr)DeleteIORequest((struct IORequest *)tr);
    if(p)DeleteMsgPort(p);

    l=Lock((STRPTR)"ENV:Sys/printer.prefs",ACCESS_READ);
    if(l){mark("OAPPre.02-envprefs","PASS ENV:Sys/printer.prefs exists\n");UnLock(l);}
    else mark("OAPPre.02-envprefs","FAIL ENV:Sys/printer.prefs missing\n");

    l=Lock((STRPTR)"DEVS:Printers/OpenPrint",ACCESS_READ);
    if(l){mark("OAPPre.03-driverfile","PASS driver file exists\n");UnLock(l);}
    else mark("OAPPre.03-driverfile","FAIL driver file missing\n");

    seg=LoadSeg((STRPTR)"DEVS:Printers/OpenPrint");
    if(!seg){mark("OAPPre.04-loadseg","FAIL LoadSeg OpenPrint\n");goto done;}
    mark("OAPPre.04-loadseg","PASS LoadSeg OpenPrint\n");
    ps=(struct PrinterSegment *)BADDR(seg);
    ped=&ps->ps_PED;
    memset(out,0,sizeof(out));
    if(ps->ps_Version==35 && ped->ped_PrinterName && ped->ped_Open && ped->ped_Init)
        strcpy(out,"PASS printer segment v35/PED pointers valid\n");
    else
        strcpy(out,"FAIL printer segment header/PED invalid\n");
    mark("OAPPre.05-segment",out);
    if(ped->ped_PrinterName && !strcmp((char *)ped->ped_PrinterName,"OpenPrint"))
        mark("OAPPre.06-name","PASS PED name OpenPrint\n");
    else
        mark("OAPPre.06-name","FAIL PED name mismatch\n");
    UnLoadSeg(seg);seg=0;

done:
    if(seg)UnLoadSeg(seg);
    mark("OAPPre.99-done","preflight completed\n");
    return 0;
}
