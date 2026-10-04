#include "oap.h"
#include "oap_selection.h"
#include "oav_jobs.h"
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

enum { G_URI=1,G_COPIES,G_PAPER,G_ORIENT,G_COLOR,G_DUPLEX,G_PAGES,G_PRINT,G_SAVE,G_CANCEL,G_BROWSE };
static STRPTR paper_labels[]={ (STRPTR)"A4",(STRPTR)"Letter",NULL };
static STRPTR orient_labels[]={ (STRPTR)"Portrait",(STRPTR)"Landscape",NULL };
static STRPTR color_labels[]={ (STRPTR)"Colour",(STRPTR)"Monochrome",NULL };
static STRPTR duplex_labels[]={ (STRPTR)"One-sided",(STRPTR)"Two-sided long",(STRPTR)"Two-sided short",NULL };
static long pdf_size(const char *p){FILE*f=fopen(p,"rb");long n=-1;if(f){if(!fseek(f,0,SEEK_END))n=ftell(f);fclose(f);}return n;}
static int copy_file(const char *a,const char *b)
{
 FILE *in,*out;char *buf;size_t n;int ok=1;if(!strcmp(a,b))return 1;
 in=fopen(a,"rb");if(!in)return 0;buf=malloc(8192);if(!buf){fclose(in);return 0;}
 out=fopen(b,"wb");if(!out){free(buf);fclose(in);return 0;}
 while((n=fread(buf,1,8192,in))!=0)if(fwrite(buf,1,n,out)!=n){ok=0;break;}
 if(ferror(in))ok=0;fclose(in);if(fclose(out))ok=0;free(buf);return ok;
}

static void parse_pages(const char *s,OAPJobOptions *o){int a=0,b=0;o->page_start=o->page_end=0;if(!s||!s[0]||!strcmp(s,"All")||!strcmp(s,"all"))return;if(sscanf(s,"%d-%d",&a,&b)==2&&a>0&&b>=a){o->page_start=a;o->page_end=b;}else if(sscanf(s,"%d",&a)==1&&a>0){o->page_start=o->page_end=a;}}
static void draw_preview(struct Window *w,const char *pdf,const char *status)
{
    struct RastPort *rp=w->RPort; char line[160]; long n=pdf_size(pdf); WORD x=w->BorderLeft+14,y=w->BorderTop+14;
    SetAPen(rp,1); RectFill(rp,x,y,x+248,y+182); SetAPen(rp,0); RectFill(rp,x+28,y+10,x+218,y+160); SetAPen(rp,1); Move(rp,x+42,y+38); Text(rp,(STRPTR)"PDF document",12);
    snprintf(line,sizeof(line),"%ld bytes",n); Move(rp,x+42,y+58); Text(rp,(STRPTR)line,strlen(line)); Move(rp,x+42,y+78); Text(rp,(STRPTR)"Page rendering follows",22);
    SetAPen(rp,0);RectFill(rp,x,y+188,w->Width-w->BorderRight-2,y+219);SetAPen(rp,1); Move(rp,x,y+202); snprintf(line,sizeof(line),"Job: %.32s",pdf); Text(rp,(STRPTR)line,strlen(line)); Move(rp,x,y+216); {size_t z=strlen(status);while(z&&TextLength(rp,(STRPTR)status,z)>w->Width-w->BorderRight-x-6)z--;Text(rp,(STRPTR)status,z);}
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
    struct Screen *scr=NULL; APTR vi=NULL; struct Gadget *list=NULL,*last,*guri,*gcopy,*gpaper,*gorient,*gcolor,*gduplex,*gpages,*gprint,*gsave,*gcancel,*gbrowse;
    OAPSelectionListener selection={0}; char pending_printer[384]="";
    struct Window *w=NULL; struct NewGadget ng; struct TextAttr ta={(STRPTR)"topaz.font",8,0,0}; char status[256]="Ready - PDF generated on Amiga"; int done=0,ret=1; char saved_uri[384]=""; int tick=0,busy=0; static char request[OAV_PATH_MAX]; static OAVRequest job;
    if(oap_selected_printer(saved_uri,sizeof(saved_uri))&&(!o->printer_uri[0]||strstr(o->printer_uri,"printer.local")))strcpy(o->printer_uri,saved_uri);
    IntuitionBase=(struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library",39); GfxBase=(struct GfxBase *)OpenLibrary((STRPTR)"graphics.library",39); GadToolsBase=OpenLibrary((STRPTR)"gadtools.library",39); AslBase=OpenLibrary((STRPTR)"asl.library",38);
    if(!IntuitionBase||!GfxBase||!GadToolsBase||!AslBase){ret=0;goto out;} scr=LockPubScreen(NULL);if(!scr){ret=0;goto out;}vi=GetVisualInfoA(scr,NULL);if(!vi){ret=0;goto out;}
    memset(&ng,0,sizeof(ng));ng.ng_TextAttr=&ta;ng.ng_VisualInfo=vi;last=CreateContext(&list);
#define NG(ID,T,L,W,H) do{ng.ng_GadgetID=(ID);ng.ng_GadgetText=(STRPTR)(T);ng.ng_LeftEdge=(L);ng.ng_TopEdge=(W);ng.ng_Width=(H);ng.ng_Height=14;ng.ng_Flags=PLACETEXT_LEFT;}while(0)
    NG(G_URI,"Printer",382,12,130); last=add_gad(last,&guri,STRING_KIND,&ng,GTST_String,(ULONG)o->printer_uri,GTST_MaxChars,383);
    ng.ng_Flags=0;ng.ng_LeftEdge=524;ng.ng_TopEdge=12;ng.ng_Width=78;ng.ng_Height=16;ng.ng_GadgetID=G_BROWSE;ng.ng_GadgetText=(STRPTR)"Browse...";last=gbrowse=CreateGadget(BUTTON_KIND,last,&ng,TAG_END);
    NG(G_COPIES,"Copies",382,38,70); last=add_gad(last,&gcopy,INTEGER_KIND,&ng,GTIN_Number,o->copies,GTIN_MaxChars,3);
    NG(G_PAPER,"Paper",532,38,70); last=add_gad(last,&gpaper,CYCLE_KIND,&ng,GTCY_Labels,(ULONG)paper_labels,GTCY_Active,o->paper);
    NG(G_ORIENT,"Layout",382,64,100); last=add_gad(last,&gorient,CYCLE_KIND,&ng,GTCY_Labels,(ULONG)orient_labels,GTCY_Active,o->orientation);
    NG(G_COLOR,"Colour",532,64,70); last=add_gad(last,&gcolor,CYCLE_KIND,&ng,GTCY_Labels,(ULONG)color_labels,GTCY_Active,o->color?0:1);
    NG(G_DUPLEX,"Sides",382,90,220); last=add_gad(last,&gduplex,CYCLE_KIND,&ng,GTCY_Labels,(ULONG)duplex_labels,GTCY_Active,o->duplex);
    NG(G_PAGES,"Pages",382,116,100); last=add_gad(last,&gpages,STRING_KIND,&ng,GTST_String,(ULONG)"All",GTST_MaxChars,31);
#undef NG
    ng.ng_Flags=0;ng.ng_GadgetText=(STRPTR)"Print";ng.ng_GadgetID=G_PRINT;ng.ng_LeftEdge=350;ng.ng_TopEdge=164;ng.ng_Width=78;ng.ng_Height=18;last=gprint=CreateGadget(BUTTON_KIND,last,&ng,TAG_END);
    ng.ng_GadgetText=(STRPTR)"Save PDF";ng.ng_GadgetID=G_SAVE;ng.ng_LeftEdge=440;ng.ng_Width=78;last=gsave=CreateGadget(BUTTON_KIND,last,&ng,TAG_END);
    ng.ng_GadgetText=(STRPTR)"Close";ng.ng_GadgetID=G_CANCEL;ng.ng_LeftEdge=530;ng.ng_Width=72;last=gcancel=CreateGadget(BUTTON_KIND,last,&ng,TAG_END);
    if(!gbrowse||!guri||!gcopy||!gpaper||!gorient||!gcolor||!gduplex||!gpages||!gprint||!gsave||!gcancel){ret=0;goto out;}
    w=OpenWindowTags(NULL,WA_Title,(ULONG)"OpenAmigaPrint - Print",WA_PubScreen,(ULONG)scr,WA_InnerWidth,620,WA_InnerHeight,238,WA_Gadgets,(ULONG)list,WA_DragBar,TRUE,WA_DepthGadget,TRUE,WA_CloseGadget,TRUE,WA_Activate,TRUE,WA_SimpleRefresh,TRUE,WA_IDCMP,IDCMP_INTUITICKS|IDCMP_CLOSEWINDOW|IDCMP_REFRESHWINDOW|STRINGIDCMP|INTEGERIDCMP|CYCLEIDCMP|BUTTONIDCMP,TAG_END); if(!w){ret=0;goto out;}
    GT_RefreshWindow(w,NULL);draw_preview(w,pdf,status);
    oap_selection_open(&selection);
    request[0]=0;
    while(!done){
        struct IntuiMessage *m;
        Wait((1UL<<w->UserPort->mp_SigBit)|oap_selection_mask(&selection));
        oap_selection_receive(&selection,pending_printer,sizeof(pending_printer));
        if(!busy && pending_printer[0]){
            strcpy(saved_uri,pending_printer);strcpy(o->printer_uri,pending_printer);
            GT_SetGadgetAttrs(guri,w,NULL,GTST_String,(ULONG)o->printer_uri,TAG_END);
            snprintf(status,sizeof(status),"Selected %s",o->printer_uri);
            pending_printer[0]=0;
            draw_preview(w,pdf,status);
        }
        while((m=GT_GetIMsg(w->UserPort))){
            ULONG cls=m->Class;
            UWORD id=m->IAddress?((struct Gadget *)m->IAddress)->GadgetID:0;
            GT_ReplyIMsg(m);
            if(cls==IDCMP_CLOSEWINDOW){if(busy)oav_cancel(request);done=1;break;}
            if(cls==IDCMP_INTUITICKS&&++tick>=5){
                tick=0;
                if(busy){
                    char state[32],msg[256];
                    if(oav_result(request,state,sizeof(state),msg,sizeof(msg))){
                        snprintf(status,sizeof(status),"%s",msg);
                        if(oav_result_terminal(state)){
                            busy=0;
                            /* An uncertain/accepted job is not a licence to send a duplicate. */
                            GT_SetGadgetAttrs(gprint,w,NULL,GA_Disabled,!strcmp(state,"uncertain")||!strcmp(state,"submitted"),TAG_END);
                            GT_SetGadgetAttrs(gcancel,w,NULL,GA_Text,(ULONG)"Close",TAG_END);
                        }
                        draw_preview(w,pdf,status);
                    }
                }else{
                    char selected[384];
                    if(oap_selected_printer(selected,sizeof(selected))&&strcmp(selected,saved_uri)){
                        strcpy(saved_uri,selected);strcpy(o->printer_uri,selected);
                        GT_SetGadgetAttrs(guri,w,NULL,GTST_String,(ULONG)selected,TAG_END);
                        snprintf(status,sizeof(status),"PDF printer selected; Print rechecks its capabilities");
                        draw_preview(w,pdf,status);
                    }
                }
            }else if(cls==IDCMP_REFRESHWINDOW){
                GT_BeginRefresh(w);GT_EndRefresh(w,TRUE);draw_preview(w,pdf,status);
            }else if(cls==IDCMP_GADGETUP){
                if(id==G_CANCEL){
                    if(busy){oav_cancel(request);snprintf(status,sizeof(status),"Stopping upload; awaiting worker result. Do not resend.");draw_preview(w,pdf,status);}
                    else done=1;
                }else if(id==G_BROWSE&&!busy){
                    snprintf(status,sizeof(status),oap_launch_printer_browser()?"Choose a verified PDF printer in Browse":"Cannot launch C:OAPPrinters");draw_preview(w,pdf,status);
                }else if(id==G_SAVE&&!busy){save_as(w,pdf,status,sizeof(status));draw_preview(w,pdf,status);}
                else if(id==G_PRINT&&!busy){
                    ULONG v;STRPTR str;
                    GT_GetGadgetAttrs(guri,w,NULL,GTST_String,(ULONG)&str,TAG_END);
                    strncpy(o->printer_uri,str,sizeof(o->printer_uri)-1);o->printer_uri[sizeof(o->printer_uri)-1]=0;
                    GT_GetGadgetAttrs(gcopy,w,NULL,GTIN_Number,(ULONG)&v,TAG_END);o->copies=v?v:1;
                    GT_GetGadgetAttrs(gpaper,w,NULL,GTCY_Active,(ULONG)&v,TAG_END);o->paper=v;
                    GT_GetGadgetAttrs(gorient,w,NULL,GTCY_Active,(ULONG)&v,TAG_END);o->orientation=v;
                    GT_GetGadgetAttrs(gcolor,w,NULL,GTCY_Active,(ULONG)&v,TAG_END);o->color=(v==0);
                    GT_GetGadgetAttrs(gduplex,w,NULL,GTCY_Active,(ULONG)&v,TAG_END);o->duplex=v;
                    GT_GetGadgetAttrs(gpages,w,NULL,GTST_String,(ULONG)&str,TAG_END);parse_pages(str,o);
                    memset(&job,0,sizeof(job));oav_layout_defaults(&job.layout);
                    strncpy(job.source,pdf,sizeof(job.source)-1);strcpy(job.action,"send");
                    strncpy(job.uri,o->printer_uri,sizeof(job.uri)-1);job.has_print_options=1;job.print=*o;
                    if(oav_submit(&job,request,sizeof(request),status,sizeof(status))){
                        busy=1;GT_SetGadgetAttrs(gprint,w,NULL,GA_Disabled,TRUE,TAG_END);
                        GT_SetGadgetAttrs(gcancel,w,NULL,GA_Text,(ULONG)"Stop",TAG_END);
                        snprintf(status,sizeof(status),"Print worker started; window remains responsive");
                    }
                    draw_preview(w,pdf,status);
                }
            }
        }
    }

out:
    oap_selection_close(&selection);
    if(w)CloseWindow(w);if(list)FreeGadgets(list);if(vi)FreeVisualInfo(vi);if(scr)UnlockPubScreen(NULL,scr);if(AslBase)CloseLibrary(AslBase);if(GadToolsBase)CloseLibrary(GadToolsBase);if(GfxBase)CloseLibrary((struct Library *)GfxBase);if(IntuitionBase)CloseLibrary((struct Library *)IntuitionBase);return ret;
}
