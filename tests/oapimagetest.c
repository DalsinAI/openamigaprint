#include <exec/types.h>
#include <exec/memory.h>
#include <devices/printer.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include "oap_spooler.h"
#include <stdio.h>
#include <string.h>

struct GfxBase *GfxBase;

static void set_palette(struct ColorMap *cm)
{
    SetRGB32CM(cm,0,0xffffffff,0xffffffff,0xffffffff);
    SetRGB32CM(cm,1,0x00000000,0x00000000,0x00000000);
    SetRGB32CM(cm,2,0xffffffff,0x00000000,0x00000000);
    SetRGB32CM(cm,3,0x00000000,0xffffffff,0x00000000);
    SetRGB32CM(cm,4,0x00000000,0x00000000,0xffffffff);
    SetRGB32CM(cm,5,0xffffffff,0xffffffff,0x00000000);
    SetRGB32CM(cm,6,0x00000000,0xffffffff,0xffffffff);
    SetRGB32CM(cm,7,0xffffffff,0x00000000,0xffffffff);
}
static int wait_for_spooler(void)
{
    int i;struct MsgPort *p;
    for(i=0;i<100;i++){Forbid();p=FindPort((STRPTR)OAP_SPOOLER_PORT);Permit();if(p)return 1;Delay(5);}
    return 0;
}
static void draw_card(struct RastPort *rp)
{
    int i;static const char title[]="OpenAmigaPrint raster test";
    SetAPen(rp,0);RectFill(rp,0,0,319,199);
    for(i=0;i<6;i++){SetAPen(rp,(UBYTE)(i+2));RectFill(rp,16+i*48,24,55+i*48,78);}
    SetAPen(rp,1);Move(rp,12,12);Draw(rp,307,12);Draw(rp,307,187);Draw(rp,12,187);Draw(rp,12,12);
    Move(rp,16,106);Text(rp,(STRPTR)title,(ULONG)strlen(title));
    SetAPen(rp,4);Move(rp,18,128);Draw(rp,296,170);
    SetAPen(rp,2);Move(rp,18,170);Draw(rp,296,128);
    SetAPen(rp,1);Move(rp,16,184);Text(rp,(STRPTR)"PRD_DUMPRPORT -> OpenAmigaPrint",31);
}
int main(int argc,char **argv)
{
    struct MsgPort *port=NULL;struct IODRPReq *io=NULL;struct BitMap *bm=NULL;struct ColorMap *cm=NULL;struct RastPort rp;int rc=20;
    if(!wait_for_spooler()){puts("OAPImageTest: spooler service not ready");goto out;}
    GfxBase=(struct GfxBase *)OpenLibrary((STRPTR)"graphics.library",39);if(!GfxBase){puts("OAPImageTest: graphics.library v39 required");goto out;}
    bm=AllocBitMap(320,200,4,BMF_CLEAR,NULL);cm=GetColorMap(16);if(!bm||!cm){puts("OAPImageTest: cannot allocate test bitmap");goto out;}
    set_palette(cm);InitRastPort(&rp);rp.BitMap=bm;draw_card(&rp);
    port=CreateMsgPort();if(!port){puts("OAPImageTest: cannot create message port");goto out;}
    io=(struct IODRPReq *)CreateIORequest(port,sizeof(*io));if(!io){puts("OAPImageTest: cannot create printer request");goto out;}
    if(OpenDevice((STRPTR)"printer.device",0,(struct IORequest *)io,0)){puts("OAPImageTest: cannot open printer.device");goto out;}
    io->io_Command=PRD_DUMPRPORT;io->io_RastPort=&rp;io->io_ColorMap=cm;io->io_Modes=0;
    io->io_SrcX=0;io->io_SrcY=0;io->io_SrcWidth=320;io->io_SrcHeight=200;
    if(argc>1 && (!strcmp(argv[1],"1TO1")||!strcmp(argv[1],"1to1"))){
        io->io_DestCols=320;io->io_DestRows=200;
        io->io_Special=SPECIAL_CENTER|SPECIAL_DENSITY2;
    } else {
        io->io_DestCols=0;io->io_DestRows=0;
        io->io_Special=SPECIAL_FULLCOLS|SPECIAL_ASPECT|SPECIAL_CENTER|SPECIAL_DENSITY2;
    }
    DoIO((struct IORequest *)io);
    if(io->io_Error){printf("OAPImageTest: PRD_DUMPRPORT error %ld\n",(long)io->io_Error);goto close_device;}
    puts("OpenAmigaPrint: raster picture job submitted");rc=0;
close_device:
    CloseDevice((struct IORequest *)io);
out:
    if(io)DeleteIORequest((struct IORequest *)io);if(port)DeleteMsgPort(port);if(cm)FreeColorMap(cm);if(bm)FreeBitMap(bm);if(GfxBase)CloseLibrary((struct Library *)GfxBase);return rc;
}
