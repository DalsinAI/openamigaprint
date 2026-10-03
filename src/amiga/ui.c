#include "oap.h"
#include <exec/types.h>
#include <exec/libraries.h>
#include <intuition/intuitionbase.h>
#include <graphics/gfxbase.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct Library *GadToolsBase,*AslBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

enum { G_URI=1,G_COPIES,G_PAPER,G_ORIENT,G_COLOR,G_DUPLEX,G_PAGES,G_PRINT,G_SAVE,G_CANCEL };
static STRPTR paper_labels[]={ (STRPTR)"A4",(STRPTR)"Letter",NULL };
static STRPTR orient_labels[]={ (STRPTR)"Portrait",(STRPTR)"Landscape",NULL };
static STRPTR color_labels[]={ (STRPTR)"Colour",(STRPTR)"Monochrome",NULL };
static STRPTR duplex_labels[]={ (STRPTR)"One-sided",(STRPTR)"Two-sided long",(STRPTR)"Two-sided short",NULL };
static long pdf_size(const char *p){FILE*f=fopen(p,"rb");long n=-1;if(f){if(!fseek(f,0,SEEK_END))n=ftell(f);fclose(f);}return n;}
static int copy_file(const char *a,const char *b){FILE*i=fopen(a,"rb"),*o;char q[8192];size_t n;if(!i)return 0;o=fopen(b,"wb");if(!o){fclose(i);return 0;}while((n=fread(q,1,sizeof(q),i))>0)if(fwrite(q,1,n,o)!=n){fclose(i);fclose(o);return 0;}fclose(i);fclose(o);return 1;}
static void parse_pages(const char *s,OAPJobOptions *o){int a=0,b=0;o->page_start=o->page_end=0;if(!s||!s[0]||!strcmp(s,"All")||!strcmp(s,"all"))return;if(sscanf(s,"%d-%d",&a,&b)==2&&a>0&&b>=a){o->page_start=a;o->page_end=b;}else if(sscanf(s,"%d",&a)==1&&a>0){o->page_start=o->page_end=a;}}
static void draw_preview(struct Window *w,const char *pdf,const char *status)
{
    struct RastPort *rp=w->RPort; char line[160]; long n=pdf_size(pdf); WORD x=w->BorderLeft+14,y=w->BorderTop+14;
    SetAPen(rp,1); RectFill(rp,x,y,x+248,y+182); SetAPen(rp,0); RectFill(rp,x+28,y+10,x+218,y+160); SetAPen(rp,1); Move(rp,x+42,y+38); Text(rp,(STRPTR)"PDF document",12);
    snprintf(line,sizeof(line),"%ld bytes",n); Move(rp,x+42,y+58); Text(rp,(STRPTR)line,strlen(line)); Move(rp,x+42,y+78); Text(rp,(STRPTR)"Page rendering follows",22);
    SetAPen(rp,1); Move(rp,x,y+202); snprintf(line,sizeof(line),"Job: %.32s",pdf); Text(rp,(STRPTR)line,strlen(line)); Move(rp,x,y+216); Text(rp,(STRPTR)status,strlen(status));
}
static int save_as(struct Window *w,const char *pdf,char *status,size_t cap)
{
    struct FileRequester *fr; char path[512]; int ok=0;
    fr=(struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,ASLFR_TitleText,(ULONG)"Save PDF",ASLFR_DoSaveMode,TRUE,ASLFR_InitialFile,(ULONG)"OpenAmigaPrint.pdf",TAG_END);
    if(fr&&AslRequestTags(fr,ASLFR_Window,(ULONG)w,TAG_END)){snprintf(path,sizeof(path),"%s%s%s",fr->fr_Drawer,fr->fr_Drawer[0]&&fr->fr_Drawer[strlen(fr->fr_Drawer)-1]!=':'?"/":"",fr->fr_File);ok=copy_file(pdf,path);snprintf(status,cap,ok?"Saved %.40s":"Save failed: %.40s",path);} if(fr)FreeAslRequest(fr); return ok;
}

static struct Gadget *add_gad(struct Gadget *prev,struct Gadget **slot,int kind,struct NewGadget *ng,Tag tag1,ULONG data1,Tag tag2,ULONG data2)
{
    *slot=CreateGadget(kind,prev,ng,tag1,data1,tag2,data2,TAG_END);return *slot;
}

int oap_run_print_dialog(const char *pdf,OAPJobOptions *o)
{
    struct Screen *scr=NULL; APTR vi=NULL; struct Gadget *list=NULL,*last,*guri,*gcopy,*gpaper,*gorient,*gcolor,*gduplex,*gpages,*gprint,*gsave,*gcancel;
    struct Window *w=NULL; struct NewGadget ng; struct TextAttr ta={(STRPTR)"topaz.font",8,0,0}; char status[96]="Ready - PDF generated on Amiga"; int done=0,ret=1;
    IntuitionBase=(struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library",39); GfxBase=(struct GfxBase *)OpenLibrary((STRPTR)"graphics.library",39); GadToolsBase=OpenLibrary((STRPTR)"gadtools.library",39); AslBase=OpenLibrary((STRPTR)"asl.library",38);
    if(!IntuitionBase||!GfxBase||!GadToolsBase||!AslBase){ret=0;goto out;} scr=LockPubScreen(NULL);if(!scr){ret=0;goto out;}vi=GetVisualInfoA(scr,NULL);if(!vi){ret=0;goto out;}
    memset(&ng,0,sizeof(ng));ng.ng_TextAttr=&ta;ng.ng_VisualInfo=vi;last=CreateContext(&list);
#define NG(ID,T,L,W,H) do{ng.ng_GadgetID=(ID);ng.ng_GadgetText=(STRPTR)(T);ng.ng_LeftEdge=(L);ng.ng_TopEdge=(W);ng.ng_Width=(H);ng.ng_Height=14;ng.ng_Flags=PLACETEXT_LEFT;}while(0)
    NG(G_URI,"Printer",382,12,220); last=add_gad(last,&guri,STRING_KIND,&ng,GTST_String,(ULONG)o->printer_uri,GTST_MaxChars,383);
    NG(G_COPIES,"Copies",382,38,70); last=add_gad(last,&gcopy,INTEGER_KIND,&ng,GTIN_Number,o->copies,GTIN_MaxChars,3);
    NG(G_PAPER,"Paper",532,38,70); last=add_gad(last,&gpaper,CYCLE_KIND,&ng,GTCY_Labels,(ULONG)paper_labels,GTCY_Active,o->paper);
    NG(G_ORIENT,"Layout",382,64,100); last=add_gad(last,&gorient,CYCLE_KIND,&ng,GTCY_Labels,(ULONG)orient_labels,GTCY_Active,o->orientation);
    NG(G_COLOR,"Colour",532,64,70); last=add_gad(last,&gcolor,CYCLE_KIND,&ng,GTCY_Labels,(ULONG)color_labels,GTCY_Active,o->color?0:1);
    NG(G_DUPLEX,"Sides",382,90,220); last=add_gad(last,&gduplex,CYCLE_KIND,&ng,GTCY_Labels,(ULONG)duplex_labels,GTCY_Active,o->duplex);
    NG(G_PAGES,"Pages",382,116,100); last=add_gad(last,&gpages,STRING_KIND,&ng,GTST_String,(ULONG)"All",GTST_MaxChars,31);
#undef NG
    ng.ng_Flags=0;ng.ng_GadgetText=(STRPTR)"Print";ng.ng_GadgetID=G_PRINT;ng.ng_LeftEdge=350;ng.ng_TopEdge=164;ng.ng_Width=78;ng.ng_Height=18;last=gprint=CreateGadget(BUTTON_KIND,last,&ng,TAG_END);
    ng.ng_GadgetText=(STRPTR)"Save PDF";ng.ng_GadgetID=G_SAVE;ng.ng_LeftEdge=440;ng.ng_Width=78;last=gsave=CreateGadget(BUTTON_KIND,last,&ng,TAG_END);
    ng.ng_GadgetText=(STRPTR)"Cancel";ng.ng_GadgetID=G_CANCEL;ng.ng_LeftEdge=530;ng.ng_Width=72;last=gcancel=CreateGadget(BUTTON_KIND,last,&ng,TAG_END);
    if(!guri||!gcopy||!gpaper||!gorient||!gcolor||!gduplex||!gpages||!gprint||!gsave||!gcancel){ret=0;goto out;}
    w=OpenWindowTags(NULL,WA_Title,(ULONG)"OpenAmigaPrint - Print",WA_PubScreen,(ULONG)scr,WA_InnerWidth,620,WA_InnerHeight,238,WA_Gadgets,(ULONG)list,WA_DragBar,TRUE,WA_DepthGadget,TRUE,WA_CloseGadget,TRUE,WA_Activate,TRUE,WA_SimpleRefresh,TRUE,WA_IDCMP,IDCMP_CLOSEWINDOW|IDCMP_REFRESHWINDOW|STRINGIDCMP|INTEGERIDCMP|CYCLEIDCMP|BUTTONIDCMP,TAG_END); if(!w){ret=0;goto out;}
    GT_RefreshWindow(w,NULL);draw_preview(w,pdf,status);
    while(!done){struct IntuiMessage *m;Wait(1UL<<w->UserPort->mp_SigBit);while((m=GT_GetIMsg(w->UserPort))){ULONG cls=m->Class;UWORD id=m->IAddress?((struct Gadget *)m->IAddress)->GadgetID:0;GT_ReplyIMsg(m);if(cls==IDCMP_CLOSEWINDOW){done=1;break;}if(cls==IDCMP_REFRESHWINDOW){GT_BeginRefresh(w);GT_EndRefresh(w,TRUE);draw_preview(w,pdf,status);}else if(cls==IDCMP_GADGETUP){if(id==G_CANCEL){done=1;}else if(id==G_SAVE){save_as(w,pdf,status,sizeof(status));draw_preview(w,pdf,status);}else if(id==G_PRINT){ULONG v;STRPTR s;GT_GetGadgetAttrs(guri,w,NULL,GTST_String,(ULONG)&s,TAG_END);strncpy(o->printer_uri,s,sizeof(o->printer_uri)-1);o->printer_uri[sizeof(o->printer_uri)-1]=0;GT_GetGadgetAttrs(gcopy,w,NULL,GTIN_Number,(ULONG)&v,TAG_END);o->copies=v?v:1;GT_GetGadgetAttrs(gpaper,w,NULL,GTCY_Active,(ULONG)&v,TAG_END);o->paper=v;GT_GetGadgetAttrs(gorient,w,NULL,GTCY_Active,(ULONG)&v,TAG_END);o->orientation=v;GT_GetGadgetAttrs(gcolor,w,NULL,GTCY_Active,(ULONG)&v,TAG_END);o->color=(v==0);GT_GetGadgetAttrs(gduplex,w,NULL,GTCY_Active,(ULONG)&v,TAG_END);o->duplex=v;GT_GetGadgetAttrs(gpages,w,NULL,GTST_String,(ULONG)&s,TAG_END);parse_pages(s,o);snprintf(status,sizeof(status),"Sending PDF to printer...");draw_preview(w,pdf,status);oap_ipp_submit_pdf(pdf,o,status,sizeof(status));draw_preview(w,pdf,status);}}}}
out:
    if(w)CloseWindow(w);if(list)FreeGadgets(list);if(vi)FreeVisualInfo(vi);if(scr)UnlockPubScreen(NULL,scr);if(AslBase)CloseLibrary(AslBase);if(GadToolsBase)CloseLibrary(GadToolsBase);if(GfxBase)CloseLibrary((struct Library *)GfxBase);if(IntuitionBase)CloseLibrary((struct Library *)IntuitionBase);return ret;
}
