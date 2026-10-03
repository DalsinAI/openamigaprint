/* SPDX-License-Identifier: BSD-2-Clause */
/* ReAction owns the chrome; datatypes.library owns the hosted content. */
#include "oav_core.h"
#include "oav_jobs.h"
#include "oap_queue.h"
#include "oap.h"
#include "oap_selection.h"
#include "oap_printers.h"
#include "oap_stack.h"
#include <exec/types.h>
#include <exec/lists.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <intuition/intuitionbase.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <graphics/gfxbase.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <datatypes/animationclass.h>
#include <libraries/amigaguide.h>
#include <libraries/asl.h>
#include <classes/window.h>
#include <classes/arexx.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/space.h>
#include <gadgets/listbrowser.h>
#include <gadgets/scroller.h>
#include <gadgets/chooser.h>
#include <images/bevel.h>
#include <libraries/gadtools.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/datatypes.h>
#include <proto/asl.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/string.h>
#include <proto/space.h>
#include <proto/listbrowser.h>
#include <proto/scroller.h>
#include <proto/chooser.h>
#include <proto/label.h>
#include <images/label.h>
#include <workbench/workbench.h>
#include <proto/arexx.h>
#include <reaction/reaction_macros.h>
#include <clib/alib_protos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>

unsigned long __stack = 65536;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *UtilityBase,*DataTypesBase,*AslBase,*WindowBase,*LayoutBase;
struct Library *ButtonBase,*StringBase,*SpaceBase,*ListBrowserBase,*ScrollerBase,*ARexxBase,*ChooserBase,*LabelBase;
#define MAX_JOBS 128
#define REG(r,t) register t __asm(#r)
enum { B_OPEN=1,B_PAGE,B_FIT,B_ONE,B_MINUS,B_PLUS,B_PLAY,B_PAUSE,B_STOP,B_PREV,B_NEXT,
 B_PAPER,B_ORIENT,B_SCALE,B_EXPORT,B_PRINT,B_QOPEN,B_REFRESH,B_SEND,B_CANCEL,B_INFO,B_LIST,B_VSCROLL,B_HSCROLL,B_URI,B_STATUS,B_FILE,B_QUIT,B_BROWSE,
 B_PRINTDLG,B_VIEW,B_PRINTERS };
typedef struct QueueRow { char name[128],path[OAV_PATH_MAX],state[32]; } QueueRow;
static struct App {
 Object *winobj,*dto,*space,*status,*file,*qg,*uri,*vscroll,*hscroll,*rx;
 Object *paper,*orient,*scalemode,*pagebutton,*cancel;
 struct MsgPort *appport;char title[160];int print_after;  /* print_after: open the Print requester when the queued PDF is ready */
 struct Window *win;struct Screen *screen;struct DrawInfo *drawinfo;
 struct Hook idcmp_hook,render_hook;struct List qlist;QueueRow jobs[MAX_JOBS];
 OAVLayout settings;OAVPlacement placement;
 struct IBox paperbox,contentbox;ULONG group,natural_w,natural_h,whitepen;
 OAPSelectionListener selection;
 char printer_saved[384];
 char path[OAV_PATH_MAX],lastreq[OAV_PATH_MAX],message[256],resultbuf[2048];
 int running,refresh,page,zoom,job_active,qcount,have_white,ticks,poll_jobs;
} A;
static void page_setup_applies(int yes);
static void copystr(char *d,size_t n,const char *s){if(n){strncpy(d,s,n-1);d[n-1]=0;}}
static void status(const char *s)
{copystr(A.message,sizeof(A.message),s);if(A.win&&A.status)SetGadgetAttrs((struct Gadget *)A.status,A.win,NULL,GA_Text,(ULONG)A.message,TAG_DONE);}
static int trigger_supported(ULONG id)
{struct DTMethod *m;int i;if(!A.dto)return 0;m=(struct DTMethod *)GetDTTriggerMethods(A.dto);if(!m)return 0;for(i=0;i<128&&m[i].dtm_Label;i++)if((m[i].dtm_Method&STMF_METHOD_MASK)==id)return 1;return 0;}
static void scroll_info(void)
{
 ULONG th=0,tv=0,vh=1,vv=1,nh=1,nv=1;if(!A.dto||!A.win)return;
 GetDTAttrs(A.dto,DTA_TopHoriz,(ULONG)&th,DTA_TopVert,(ULONG)&tv,DTA_VisibleHoriz,(ULONG)&vh,DTA_VisibleVert,(ULONG)&vv,DTA_TotalHoriz,(ULONG)&nh,DTA_TotalVert,(ULONG)&nv,TAG_DONE);
 SetGadgetAttrs((struct Gadget *)A.vscroll,A.win,NULL,SCROLLER_Top,tv,SCROLLER_Visible,vv,SCROLLER_Total,nv,TAG_DONE);
 SetGadgetAttrs((struct Gadget *)A.hscroll,A.win,NULL,SCROLLER_Top,th,SCROLLER_Visible,vh,SCROLLER_Total,nh,TAG_DONE);
}
static ULONG idcmp_hook(REG(a0,struct Hook *h),REG(a2,Object *o),REG(a1,struct IntuiMessage *im))
{
 (void)h;(void)o;if(im->Class==IDCMP_IDCMPUPDATE){A.refresh=1;}
 if(im->Class==IDCMP_REFRESHWINDOW)A.refresh=1;
 if(im->Class==IDCMP_INTUITICKS&&++A.ticks>=10){A.ticks=0;A.poll_jobs=1;}
 return 0;
}
static ULONG render_hook(REG(a0,struct Hook *h),REG(a2,Object *o),REG(a1,struct gpRender *r))
{
 struct IBox *b=NULL;(void)h;GetAttr(SPACE_AreaBox,o,(ULONG *)&b);if(!b||b->Width<1||b->Height<1)return 0;
 SetAPen(r->gpr_RPort,A.drawinfo?A.drawinfo->dri_Pens[BACKGROUNDPEN]:0);
 RectFill(r->gpr_RPort,b->Left,b->Top,b->Left+b->Width-1,b->Top+b->Height-1);
 if(A.page&&A.group==GID_PICTURE&&A.paperbox.Width>0){
  SetAPen(r->gpr_RPort,A.drawinfo?A.drawinfo->dri_Pens[SHADOWPEN]:1);
  RectFill(r->gpr_RPort,A.paperbox.Left-1,A.paperbox.Top-1,A.paperbox.Left+A.paperbox.Width,A.paperbox.Top+A.paperbox.Height);
  SetAPen(r->gpr_RPort,A.whitepen);RectFill(r->gpr_RPort,A.paperbox.Left,A.paperbox.Top,A.paperbox.Left+A.paperbox.Width-1,A.paperbox.Top+A.paperbox.Height-1);
 }
 A.refresh=1;return 0;
}
static void close_content(void)
{if(A.dto){if(A.win)RemoveDTObject(A.win,A.dto);DisposeDTObject(A.dto);A.dto=NULL;}A.group=0;}
static int load_file(const char *path)
{
 Object *dto;struct DataType *dt=NULL;struct IBox *area=NULL;struct BitMapHeader *bmh=NULL;
 ULONG group=0,w=1,h=1,sw,sh;LONG left,top,dw,dh,cropx=0,cropy=0;
 struct pdtScale scale;struct gpLayout prep;struct FrameInfo fi;struct dtFrameBox frame;char msg[256];FILE *f;long bytes;
 if(!oav_safe_field(path)){status("Invalid or overlong file path");return 0;}
 {const char *ext=strrchr(path,'.');if(ext&&(!strcasecmp(ext,".docx")||!strcasecmp(ext,".pptx"))){status(oav_format_note(path));return 0;}}
 f=fopen(path,"rb");if(!f){status("Cannot open source file");return 0;}fseek(f,0,SEEK_END);bytes=ftell(f);fclose(f);
 if(bytes<0||bytes>(long)OAV_FILE_LIMIT){status("Source exceeds the 64 MiB viewer file limit");return 0;}
 status("Loading datatype...");
 dto=NewDTObject((APTR)path,DTA_SourceType,DTST_FILE,ICA_TARGET,ICTARGET_IDCMP,PDTA_DestMode,PMODE_V43,PDTA_Screen,(ULONG)A.screen,PDTA_Remap,TRUE,AGA_Secure,TRUE,DTA_ControlPanel,TRUE,GA_ID,1000,TAG_DONE);
 if(!dto){
  LONG err=IoErr();const char *ext=strrchr(path,'.');const char *name=FilePart((STRPTR)path);
  if(ext&&!strcasecmp(ext,".pdf"))snprintf(msg,sizeof(msg),"%.60s: no PDF datatype is installed. Use Print to print it with OpenAmigaPrint, or install a PDF datatype.",name);
  else if(err==ERROR_OBJECT_WRONG_TYPE||err==2000)snprintf(msg,sizeof(msg),"%.60s: no datatype on this Amiga can open this kind of file",name);
  else snprintf(msg,sizeof(msg),"%.60s couldn't be opened (DOS error %ld)",name,(long)err);
  copystr(A.path,sizeof(A.path),path);status(msg);return 0;}
 memset(&fi,0,sizeof(fi));memset(&frame,0,sizeof(frame));
 frame.MethodID=DTM_FRAMEBOX;frame.dtf_ContentsInfo=&fi;frame.dtf_FrameInfo=&fi;frame.dtf_SizeFrameInfo=sizeof(fi);
 DoDTMethodA(dto,NULL,NULL,(Msg)&frame);
 GetDTAttrs(dto,DTA_DataType,(ULONG)&dt,DTA_NominalHoriz,(ULONG)&w,DTA_NominalVert,(ULONG)&h,TAG_DONE);
 if(dt&&dt->dtn_Header)group=dt->dtn_Header->dth_GroupID;
 if(group==GID_PICTURE){GetDTAttrs(dto,PDTA_BitMapHeader,(ULONG)&bmh,TAG_DONE);if(bmh){w=bmh->bmh_Width;h=bmh->bmh_Height;}}
 if(!w)w=1;if(!h)h=1;
 if(group==GID_PICTURE&&w>OAV_PIXEL_LIMIT/h){DisposeDTObject(dto);status("Picture exceeds 16 megapixel preview limit");return 0;}
 GetAttr(SPACE_AreaBox,A.space,(ULONG *)&area);
 if(!area){DisposeDTObject(dto);status("Preview area unavailable");return 0;}
 left=area->Left+2;top=area->Top+2;dw=area->Width-4;dh=area->Height-4;sw=w;sh=h;
 A.paperbox.Width=0;
 if(group==GID_PICTURE){
  if(A.page&&oav_place(&A.settings,w,h,&A.placement)){
   if((long)(dw-12)*A.placement.page_h<=(long)(dh-12)*A.placement.page_w){
    A.paperbox.Width=(WORD)(dw-12);A.paperbox.Height=(WORD)oav_scale(A.placement.page_h,dw-12,A.placement.page_w);
   }else{A.paperbox.Height=(WORD)(dh-12);A.paperbox.Width=(WORD)oav_scale(A.placement.page_w,dh-12,A.placement.page_h);}
   A.paperbox.Left=(WORD)(left+(dw-A.paperbox.Width)/2);A.paperbox.Top=(WORD)(top+(dh-A.paperbox.Height)/2);
   sw=(ULONG)oav_scale(A.placement.w,A.paperbox.Width,A.placement.page_w);sh=(ULONG)oav_scale(A.placement.h,A.paperbox.Width,A.placement.page_w);
   left=A.paperbox.Left+oav_scale(A.placement.x,A.paperbox.Width,A.placement.page_w);top=A.paperbox.Top+oav_scale(A.placement.page_h-A.placement.y-A.placement.h,A.paperbox.Width,A.placement.page_w);
   {LONG cx=A.paperbox.Left+oav_scale(A.placement.clip_x,A.paperbox.Width,A.placement.page_w),cy=A.paperbox.Top+oav_scale(A.placement.clip_y,A.paperbox.Width,A.placement.page_w);
    LONG cw=oav_scale(A.placement.clip_w,A.paperbox.Width,A.placement.page_w),ch=oav_scale(A.placement.clip_h,A.paperbox.Width,A.placement.page_w);
    cropx=left<cx?cx-left:0;cropy=top<cy?cy-top:0;if(left<cx)left=cx;if(top<cy)top=cy;
    dw=(LONG)sw-cropx;dh=(LONG)sh-cropy;if(left+dw>cx+cw)dw=cx+cw-left;if(top+dh>cy+ch)dh=cy+ch-top;}
  }else{
   if(A.zoom){sw=w*(ULONG)A.zoom/100;sh=h*(ULONG)A.zoom/100;}
   else if((ULONG)dw*h<=(ULONG)dh*w){sw=(ULONG)dw;sh=(h*(ULONG)dw+w/2)/w;}
   else{sh=(ULONG)dh;sw=(w*(ULONG)dh+h/2)/h;}
  }
  if(sw<1)sw=1;if(sh<1)sh=1;
  if(sw<=8192&&sh<=8192&&(sw!=w||sh!=h)){
   scale.MethodID=PDTM_SCALE;scale.ps_NewWidth=sw;scale.ps_NewHeight=sh;scale.ps_Flags=0;
   if(!DoMethodA(dto,(Msg)&scale)){A.paperbox.Width=0;left=area->Left+2;top=area->Top+2;dw=area->Width-4;dh=area->Height-4;cropx=cropy=0;}
  }
 }
 if(dw<1)dw=1;if(dh<1)dh=1;
 SetDTAttrs(dto,NULL,NULL,GA_Left,left,GA_Top,top,GA_Width,dw,GA_Height,dh,DTA_TopHoriz,cropx,DTA_TopVert,cropy,TAG_DONE);
 if(group==GID_PICTURE){
  memset(&prep,0,sizeof(prep));prep.MethodID=DTM_PROCLAYOUT;prep.gpl_Initial=TRUE;
  if(!DoDTMethodA(dto,NULL,NULL,(Msg)&prep)){DisposeDTObject(dto);status("Picture layout failed before display");return 0;}
 }
 close_content();A.dto=dto;A.group=group;A.natural_w=w;A.natural_h=h;
 copystr(A.path,sizeof(A.path),path);
 if(AddDTObject(A.win,NULL,dto,-1)<0){DisposeDTObject(dto);A.dto=NULL;status("Datatype could not attach to the preview window");return 0;}
 snprintf(A.title,sizeof(A.title),"OpenAmigaView: %.120s",FilePart((STRPTR)A.path));SetAttrs(A.winobj,WA_Title,(ULONG)A.title,TAG_DONE);
 /* animations and sounds bring their own player bar (DTA_ControlPanel);
  * page setup is for pictures, the only thing laid out on paper here */
 page_setup_applies(group==GID_PICTURE);
 snprintf(msg,sizeof(msg),"%.60s \xb7 %s, %lu \xd7 %lu%s",FilePart((STRPTR)path),dt&&dt->dtn_Header?(char *)dt->dtn_Header->dth_Name:"Datatype",(unsigned long)w,(unsigned long)h,group==GID_ANIMATION?" \xb7 animation":"");
 status(msg);RefreshGList((struct Gadget *)A.space,A.win,NULL,1);A.refresh=1;return 1;
}
static void reload(void){char path[OAV_PATH_MAX];if(!A.path[0])return;copystr(path,sizeof(path),A.path);load_file(path);}
static int choose_file(char *path,int save)
{
 struct FileRequester *r=AllocAslRequestTags(ASL_FileRequest,ASLFR_Window,(ULONG)A.win,ASLFR_TitleText,(ULONG)(save?"Save PDF (new filename)":"Open document, picture or animation"),ASLFR_DoSaveMode,save,ASLFR_InitialDrawer,(ULONG)"Work:",ASLFR_InitialFile,(ULONG)(save?"Picture.pdf":""),TAG_DONE);int ok=0;
 if(r){if(AslRequestTags(r,TAG_DONE)){copystr(path,OAV_PATH_MAX,(char *)r->fr_Drawer);if(AddPart((STRPTR)path,r->fr_File,OAV_PATH_MAX))ok=1;}FreeAslRequest(r);}return ok;
}
static int qcmp(const void *a,const void *b){return strcmp(((const QueueRow *)a)->name,((const QueueRow *)b)->name);}
static void scan_queue(void)
{
 DIR *d;struct dirent *e;int i;LONG selection=-1;
 if(A.win&&A.qg){GetAttr(LISTBROWSER_Selected,A.qg,(ULONG *)&selection);SetGadgetAttrs((struct Gadget *)A.qg,A.win,NULL,LISTBROWSER_Labels,(ULONG)-1,TAG_DONE);}
 FreeListBrowserList(&A.qlist);NewList(&A.qlist);A.qcount=0;
 d=opendir(OAP_QUEUE_DIR);if(d){while((e=readdir(d))&&A.qcount<MAX_JOBS){size_t n=strlen(e->d_name);FILE *f;char mp[OAV_PATH_MAX],line[256];QueueRow *r;
  if(n<5||strcmp(e->d_name+n-4,".job"))continue;r=&A.jobs[A.qcount];
  snprintf(mp,sizeof(mp),OAP_QUEUE_DIR "/%s",e->d_name);f=fopen(mp,"r");if(!f)continue;
  snprintf(r->path,sizeof(r->path),OAP_QUEUE_DIR "/%.*s.pdf",(int)(n-4),e->d_name);snprintf(r->name,sizeof(r->name),"%.*s",(int)(n-4),e->d_name);strcpy(r->state,"queued");
  while(fgets(line,sizeof(line),f))if(!strncmp(line,"state=",6)){char *p;copystr(r->state,sizeof(r->state),line+6);p=strpbrk(r->state,"\r\n");if(p)*p=0;}
  fclose(f);A.qcount++;
 }closedir(d);}qsort(A.jobs,(size_t)A.qcount,sizeof(A.jobs[0]),qcmp);
 for(i=0;i<A.qcount;i++){struct Node *n=AllocListBrowserNode(2,LBNA_Column,0,LBNCA_Text,(ULONG)A.jobs[i].name,LBNA_Column,1,LBNCA_Text,(ULONG)A.jobs[i].state,TAG_DONE);if(n)AddTail(&A.qlist,n);}
 if(A.win&&A.qg)SetGadgetAttrs((struct Gadget *)A.qg,A.win,NULL,LISTBROWSER_Labels,(ULONG)&A.qlist,LISTBROWSER_Selected,selection<A.qcount?selection:-1,TAG_DONE);
}
static int selected(void){LONG i=-1;if(!A.qg)return -1;GetAttr(LISTBROWSER_Selected,A.qg,(ULONG *)&i);return i>=0&&i<A.qcount?(int)i:-1;}
static int start_job(const char *action,const char *output,const char *source)
{
 OAVRequest r;ULONG uri=0;char err[256];if(A.job_active){char st[32],msg[256];if(oav_result(A.lastreq,st,sizeof(st),msg,sizeof(msg))&&oav_result_terminal(st))A.job_active=0;}if(A.job_active){status("Still preparing the last page; wait, or choose Stop preparing");return 0;}
 memset(&r,0,sizeof(r));copystr(r.source,sizeof(r.source),source?source:A.path);copystr(r.action,sizeof(r.action),action);if(output)copystr(r.output,sizeof(r.output),output);
 if(A.uri)GetAttr(STRINGA_TextVal,A.uri,&uri);if(uri)copystr(r.uri,sizeof(r.uri),(char *)uri);else copystr(r.uri,sizeof(r.uri),A.printer_saved);r.layout=A.settings;
 if(oav_submit(&r,A.lastreq,sizeof(A.lastreq),err,sizeof(err))){A.job_active=1;if(A.win)SetGadgetAttrs((struct Gadget *)A.cancel,A.win,NULL,GA_Disabled,FALSE,TAG_DONE);status(err);return 1;}status(err);return 0;
}
static int do_trigger(ULONG id)
{
 struct dtTrigger t;if(!A.dto){status("Open a document first");return 0;}
 if(!trigger_supported(id)){
  if(A.group==GID_ANIMATION&&(id==STM_PAUSE||id==STM_STOP)){
   struct adtStart a;a.MethodID=id==STM_PAUSE?ADTM_PAUSE:ADTM_STOP;a.asa_Frame=0;
   if(DoMethodA(A.dto,(Msg)&a))return 1;
  }
  status("This datatype does not advertise that playback/navigation action");return 0;
 }
 memset(&t,0,sizeof(t));t.MethodID=DTM_TRIGGER;t.dtt_Function=id;DoDTMethodA(A.dto,A.win,NULL,(Msg)&t);return 1;
}
/* Print hands over to the Print requester (one way to print): a PDF as it
 * is, anything else as a PDF the worker makes first (see print_after). */
static int run_program(const char *program,const char *arg)
{
 char path[256],cmd[OAV_PATH_MAX+300];BPTR in=Open((STRPTR)"NIL:",MODE_OLDFILE),out=Open((STRPTR)"NIL:",MODE_NEWFILE);
 oap_program_path(program,path,sizeof(path));
 if(arg)snprintf(cmd,sizeof(cmd),"\"%s\" \"%s\"",path,arg);else snprintf(cmd,sizeof(cmd),"\"%s\"",path);
 if(!in||!out||SystemTags((STRPTR)cmd,SYS_Asynch,TRUE,SYS_Input,in,SYS_Output,out,NP_StackSize,65536,TAG_DONE)==-1){if(in)Close(in);if(out)Close(out);return 0;}
 return 1;
}
static int print_dialog(void)
{
 const char *ext;
 if(!A.path[0]){status("Open something to print first");return 0;}
 ext=strrchr(A.path,'.');
 if(ext&&!strcasecmp(ext,".pdf")){status(run_program("OpenAmigaPrint",A.path)?"The Print window is open":"Couldn't open OpenAmigaPrint");return 1;}
 if(A.group!=GID_PICTURE){status("Only pictures and PDFs can be printed from here for now");return 0;}
 if(!start_job("queue",NULL,NULL))return 0;
 A.print_after=1;status("Preparing the page for printing...");return 1;
}
static int open_printers(void)
{status(run_program("OAPPrinters",NULL)?"Printers and Queue is open":"Couldn't open OAPPrinters");return 1;}
static int action(int id,const char *arg)
{
 char path[OAV_PATH_MAX];int i;ULONG v;
 switch(id){
 case B_OPEN:if(arg)return load_file(arg);if(choose_file(path,0))return load_file(path);return 0;
 case B_PAGE:A.page=!A.page;SetGadgetAttrs((struct Gadget *)A.pagebutton,A.win,NULL,CHOOSER_Selected,A.page?0:1,TAG_DONE);reload();return 1;
 case B_VIEW:GetAttr(CHOOSER_Selected,A.pagebutton,&v);A.page=v==0;reload();return 1;
 case B_FIT:A.page=0;A.zoom=0;reload();return 1;
 case B_ONE:A.page=0;A.zoom=100;reload();return 1;
 case B_MINUS:A.page=0;A.zoom=A.zoom?A.zoom-25:75;if(A.zoom<25)A.zoom=25;reload();return 1;
 case B_PLUS:A.page=0;A.zoom=A.zoom?A.zoom+25:125;if(A.zoom>400)A.zoom=400;reload();return 1;
 case B_PAPER:if(arg)A.settings.paper=!A.settings.paper;else{GetAttr(CHOOSER_Selected,A.paper,&v);A.settings.paper=v?OAV_LETTER:OAV_A4;}SetGadgetAttrs((struct Gadget *)A.paper,A.win,NULL,CHOOSER_Selected,A.settings.paper?1:0,TAG_DONE);reload();return 1;
 case B_ORIENT:if(arg)A.settings.landscape=!A.settings.landscape;else{GetAttr(CHOOSER_Selected,A.orient,&v);A.settings.landscape=v!=0;}SetGadgetAttrs((struct Gadget *)A.orient,A.win,NULL,CHOOSER_Selected,A.settings.landscape?1:0,TAG_DONE);reload();return 1;
 case B_SCALE:if(arg)A.settings.scale=!A.settings.scale;else{GetAttr(CHOOSER_Selected,A.scalemode,&v);A.settings.scale=v?OAV_FILL:OAV_FIT;}SetGadgetAttrs((struct Gadget *)A.scalemode,A.win,NULL,CHOOSER_Selected,A.settings.scale?1:0,TAG_DONE);reload();return 1;
 case B_EXPORT:if(arg)return start_job("export",arg,NULL);if(choose_file(path,1))return start_job("export",path,NULL);return 0;
 case B_PRINT:return start_job("queue",NULL,NULL);
 case B_PRINTDLG:return print_dialog();
 case B_QOPEN:i=selected();if(i>=0)return load_file(A.jobs[i].path);status("Select a queued job first");return 0;
 case B_REFRESH:scan_queue();return 1;
 case B_BROWSE:case B_PRINTERS:return open_printers();
 case B_SEND:i=selected();if(i>=0)return start_job("send",NULL,A.jobs[i].path);status("Select a queued PDF and enter its IPP destination");return 0;
 case B_CANCEL:if(A.job_active&&oav_cancel(A.lastreq)){A.print_after=0;status("Stopping: the worker stops at its next safe point");return 1;}status("Nothing is being prepared");return 0;
 case B_PLAY:return do_trigger(STM_PLAY);case B_PAUSE:return do_trigger(STM_PAUSE);case B_STOP:return do_trigger(STM_STOP);
 case B_PREV:return do_trigger(STM_BROWSE_PREV);case B_NEXT:return do_trigger(STM_BROWSE_NEXT);
 case B_VSCROLL:if(A.dto){GetAttr(SCROLLER_Top,A.vscroll,&v);SetDTAttrs(A.dto,A.win,NULL,DTA_TopVert,v,TAG_DONE);}return 1;
 case B_HSCROLL:if(A.dto){GetAttr(SCROLLER_Top,A.hscroll,&v);SetDTAttrs(A.dto,A.win,NULL,DTA_TopHoriz,v,TAG_DONE);}return 1;
 case B_INFO:status(oav_format_note(A.path));return 1;
 case B_QUIT:A.running=0;return 1;
 default:return 0;
 }
}
static int set_option(const char *key,const char *val)
{
 OAVLayout proposed=A.settings;long margin;
 if(!key||!val)return 0;
 if(!strcasecmp(key,"PAPER")){if(!strcasecmp(val,"A4"))proposed.paper=OAV_A4;else if(!strcasecmp(val,"LETTER"))proposed.paper=OAV_LETTER;else return 0;}
 else if(!strcasecmp(key,"ORIENTATION")){if(!strcasecmp(val,"PORTRAIT"))proposed.landscape=0;else if(!strcasecmp(val,"LANDSCAPE"))proposed.landscape=1;else return 0;}
 else if(!strcasecmp(key,"SCALE")){if(!strcasecmp(val,"FIT"))proposed.scale=OAV_FIT;else if(!strcasecmp(val,"FILL"))proposed.scale=OAV_FILL;else return 0;}
 else if(!strcasecmp(key,"MARGIN")){if(!oav_parse_points(val,&margin))return 0;proposed.margin_cpt=margin;}
 else if(!strcasecmp(key,"PRINTER")){if(!oav_safe_field(val)||strlen(val)>=384)return 0;copystr(A.printer_saved,sizeof(A.printer_saved),val);return 1;}
 else return 0;
 A.settings=proposed;
 SetGadgetAttrs((struct Gadget *)A.paper,A.win,NULL,CHOOSER_Selected,A.settings.paper?1:0,TAG_DONE);
 SetGadgetAttrs((struct Gadget *)A.orient,A.win,NULL,CHOOSER_Selected,A.settings.landscape?1:0,TAG_DONE);
 SetGadgetAttrs((struct Gadget *)A.scalemode,A.win,NULL,CHOOSER_Selected,A.settings.scale?1:0,TAG_DONE);
 reload();return 1;
}
static void rx_command(REG(a0,struct ARexxCmd *c),REG(a1,struct RexxMsg *rm))
{
 int ok=1;const char *arg=c->ac_ArgList?(char *)c->ac_ArgList[0]:NULL;(void)rm;c->ac_RC=0;c->ac_RC2=0;c->ac_Result=NULL;
 if(c->ac_ID==200){snprintf(A.resultbuf,sizeof(A.resultbuf),"OpenAmigaView %s ReAction",OAV_VERSION);}
 else if(c->ac_ID==201){copystr(A.resultbuf,sizeof(A.resultbuf),"OPEN FILE | SET KEY VALUE | PRINT | SAVEPDF FILE | PLAY | PAUSE | STOP | NEXT | PREVIOUS | FIT | ACTUAL | PAGE | PAPER | ORIENTATION | SCALE | JOB | CANCEL | JOBS | REFRESH | QUIT");}
 else if(c->ac_ID==202){char st[32],msg[256];if(!A.lastreq[0])copystr(A.resultbuf,sizeof(A.resultbuf),"none");else if(oav_result(A.lastreq,st,sizeof(st),msg,sizeof(msg)))snprintf(A.resultbuf,sizeof(A.resultbuf),"%s %s %s",st,A.lastreq,msg);else snprintf(A.resultbuf,sizeof(A.resultbuf),"running %s",A.lastreq);}
 else if(c->ac_ID==203){int i;size_t n=0;A.resultbuf[0]=0;scan_queue();for(i=0;i<A.qcount&&n<sizeof(A.resultbuf)-160;i++){int len=snprintf(A.resultbuf+n,sizeof(A.resultbuf)-n,"%s %s\n",A.jobs[i].name,A.jobs[i].state);if(len>0)n+=(size_t)len;}}
 else if(c->ac_ID==204){ok=set_option(arg,(char *)c->ac_ArgList[1]);copystr(A.resultbuf,sizeof(A.resultbuf),ok?"Settings applied":"Invalid SET key or value");}
 else{ok=action(c->ac_ID,(c->ac_ID==B_PAPER||c->ac_ID==B_ORIENT||c->ac_ID==B_SCALE)?"toggle":arg);copystr(A.resultbuf,sizeof(A.resultbuf),(c->ac_ID==B_PRINT||c->ac_ID==B_EXPORT)&&ok?A.lastreq:A.message);}
 if(!ok){c->ac_RC=10;c->ac_RC2=1;}c->ac_Result=(STRPTR)A.resultbuf;
}
#define RX(n,id,t) {(STRPTR)n,id,(VOID (*)())rx_command,(STRPTR)t,0,NULL,0,0,NULL}
static struct ARexxCmd commands[]={
 RX("SET",204,"KEY/A,VALUE/A"),RX("VERSION",200,""),RX("HELP",201,""),RX("OPEN",B_OPEN,"FILE/A"),RX("SAVEPDF",B_EXPORT,"FILE/A"),RX("PRINT",B_PRINT,""),
 RX("PLAY",B_PLAY,""),RX("PAUSE",B_PAUSE,""),RX("STOP",B_STOP,""),RX("NEXT",B_NEXT,""),RX("PREVIOUS",B_PREV,""),
 RX("FIT",B_FIT,""),RX("ACTUAL",B_ONE,""),RX("PAGE",B_PAGE,""),RX("PAPER",B_PAPER,""),RX("ORIENTATION",B_ORIENT,""),RX("SCALE",B_SCALE,""),
 RX("JOB",202,""),RX("JOBS",203,""),RX("CANCEL",B_CANCEL,""),RX("REFRESH",B_REFRESH,""),RX("QUIT",B_QUIT,""),{NULL,0,NULL,NULL,0,NULL,0,0,NULL}};
static int libraries(void)
{
 IntuitionBase=(struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library",39);
 GfxBase=(struct GfxBase *)OpenLibrary((STRPTR)"graphics.library",39);
 UtilityBase=OpenLibrary((STRPTR)"utility.library",39);DataTypesBase=OpenLibrary((STRPTR)"datatypes.library",44);AslBase=OpenLibrary((STRPTR)"asl.library",39);
 WindowBase=OpenLibrary((STRPTR)"window.class",44);LayoutBase=OpenLibrary((STRPTR)"gadgets/layout.gadget",44);
 ButtonBase=OpenLibrary((STRPTR)"gadgets/button.gadget",44);StringBase=OpenLibrary((STRPTR)"gadgets/string.gadget",44);
 SpaceBase=OpenLibrary((STRPTR)"gadgets/space.gadget",44);ListBrowserBase=OpenLibrary((STRPTR)"gadgets/listbrowser.gadget",44);
 ScrollerBase=OpenLibrary((STRPTR)"gadgets/scroller.gadget",44);ARexxBase=OpenLibrary((STRPTR)"arexx.class",44);
 ChooserBase=OpenLibrary((STRPTR)"gadgets/chooser.gadget",44);LabelBase=OpenLibrary((STRPTR)"images/label.image",44);
 return IntuitionBase&&GfxBase&&UtilityBase&&DataTypesBase&&AslBase&&WindowBase&&LayoutBase&&ButtonBase&&StringBase&&SpaceBase&&ListBrowserBase&&ScrollerBase&&ARexxBase&&ChooserBase&&LabelBase;
}
static Object *make_button(const char *label,ULONG id)
{return NewObject(BUTTON_GetClass(),NULL,GA_Text,(ULONG)label,GA_ID,id,GA_RelVerify,TRUE,TAG_DONE);}
static Object *make_label(const char *text){return NewObject(LABEL_GetClass(),NULL,LABEL_Text,(ULONG)text,TAG_DONE);}
static Object *make_chooser(ULONG id,STRPTR *labels,int selected)
{return NewObject(CHOOSER_GetClass(),NULL,GA_ID,id,GA_RelVerify,TRUE,CHOOSER_PopUp,TRUE,CHOOSER_LabelArray,(ULONG)labels,CHOOSER_Selected,selected,TAG_DONE);}
static STRPTR view_labels[]={(STRPTR)"As the page",(STRPTR)"As the file",NULL};
static STRPTR paper_labels[]={(STRPTR)"A4",(STRPTR)"Letter",NULL};
static STRPTR orient_labels[]={(STRPTR)"Portrait",(STRPTR)"Landscape",NULL};
static STRPTR scale_labels[]={(STRPTR)"Fit to page",(STRPTR)"Fill and crop",NULL};
static struct NewMenu menus[]={
 {NM_TITLE,(STRPTR)"Project",NULL,0,0,NULL},
 {NM_ITEM,(STRPTR)"Open...",(STRPTR)"O",0,0,(APTR)B_OPEN},
 {NM_ITEM,(STRPTR)"Save as PDF...",(STRPTR)"S",0,0,(APTR)B_EXPORT},
 {NM_ITEM,(STRPTR)"About this file",(STRPTR)"I",0,0,(APTR)B_INFO},
 {NM_ITEM,NM_BARLABEL,NULL,0,0,NULL},
 {NM_ITEM,(STRPTR)"Quit",(STRPTR)"Q",0,0,(APTR)B_QUIT},
 {NM_TITLE,(STRPTR)"View",NULL,0,0,NULL},
 {NM_ITEM,(STRPTR)"Fit in window",(STRPTR)"F",0,0,(APTR)B_FIT},
 {NM_ITEM,(STRPTR)"Actual size",(STRPTR)"1",0,0,(APTR)B_ONE},
 {NM_ITEM,(STRPTR)"Zoom in",(STRPTR)"+",0,0,(APTR)B_PLUS},
 {NM_ITEM,(STRPTR)"Zoom out",(STRPTR)"-",0,0,(APTR)B_MINUS},
 {NM_ITEM,(STRPTR)"As the page / as the file",(STRPTR)"L",0,0,(APTR)B_PAGE},
 {NM_TITLE,(STRPTR)"Print",NULL,0,0,NULL},
 {NM_ITEM,(STRPTR)"Print...",(STRPTR)"P",0,0,(APTR)B_PRINTDLG},
 {NM_ITEM,(STRPTR)"Printers and queue...",(STRPTR)"R",0,0,(APTR)B_PRINTERS},
 {NM_ITEM,(STRPTR)"Stop preparing",(STRPTR)".",0,0,(APTR)B_CANCEL},
 {NM_END,NULL,NULL,0,0,NULL}};
/* Open, view, then print: the picture fills the window, page setup sits
 * beside it as labelled choices, and printing is the Print requester's job. */
static void page_setup_applies(int yes)
{
 Object *g[4];int i;g[0]=A.paper;g[1]=A.orient;g[2]=A.scalemode;g[3]=A.pagebutton;
 for(i=0;i<4;i++)SetGadgetAttrs((struct Gadget *)g[i],A.win,NULL,GA_Disabled,!yes,TAG_DONE);
}
static int window_create(void)
{
 ULONG rxerr=0;Object *zoom,*setup;
 A.screen=LockPubScreen(NULL);if(!A.screen)return 0;A.drawinfo=GetScreenDrawInfo(A.screen);
 A.whitepen=ObtainBestPen(A.screen->ViewPort.ColorMap,0xffffffffUL,0xffffffffUL,0xffffffffUL,TAG_DONE);A.have_white=A.whitepen!=(ULONG)-1;
 if(!A.have_white)A.whitepen=A.drawinfo?A.drawinfo->dri_Pens[SHINEPEN]:2;
 A.idcmp_hook.h_Entry=(ULONG (*)())idcmp_hook;A.render_hook.h_Entry=(ULONG (*)())render_hook;
 A.rx=NewObject(AREXX_GetClass(),NULL,AREXX_HostName,(ULONG)"OPENAMIGAVIEW",AREXX_NoSlot,TRUE,AREXX_Commands,(ULONG)commands,AREXX_ErrorCode,(ULONG)&rxerr,TAG_DONE);
 if(!A.rx){fprintf(stderr,"OpenAmigaView: ARexx host error %lu (another viewer may be open)\n",(unsigned long)rxerr);return 0;}
 A.appport=CreateMsgPort();
 zoom=NewObject(LAYOUT_GetClass(),NULL,LAYOUT_Orientation,LAYOUT_ORIENT_HORIZ,LAYOUT_EvenSize,TRUE,
  LAYOUT_AddChild,(ULONG)make_button("Fit",B_FIT),LAYOUT_AddChild,(ULONG)make_button("1:1",B_ONE),
  LAYOUT_AddChild,(ULONG)make_button("-",B_MINUS),LAYOUT_AddChild,(ULONG)make_button("+",B_PLUS),TAG_DONE);
 setup=NewObject(LAYOUT_GetClass(),NULL,LAYOUT_Orientation,LAYOUT_ORIENT_VERT,LAYOUT_BevelStyle,BVS_GROUP,LAYOUT_Label,(ULONG)"Page setup",
  LAYOUT_AddChild,(ULONG)(A.paper=make_chooser(B_PAPER,paper_labels,A.settings.paper?1:0)),CHILD_Label,(ULONG)make_label("Paper"),
  LAYOUT_AddChild,(ULONG)(A.orient=make_chooser(B_ORIENT,orient_labels,A.settings.landscape?1:0)),CHILD_Label,(ULONG)make_label("Turn"),
  LAYOUT_AddChild,(ULONG)(A.scalemode=make_chooser(B_SCALE,scale_labels,A.settings.scale?1:0)),CHILD_Label,(ULONG)make_label("Size"),
  TAG_DONE);
 A.status=NewObject(BUTTON_GetClass(),NULL,GA_ID,B_STATUS,GA_ReadOnly,TRUE,BUTTON_BevelStyle,BVS_THIN,BUTTON_Justification,BCJ_LEFT,
  GA_Text,(ULONG)A.message,TAG_DONE);
 A.winobj=NewObject(WINDOW_GetClass(),NULL,
  WA_Title,(ULONG)"OpenAmigaView",WA_ScreenTitle,(ULONG)"OpenAmigaView: open, look, print",
  WA_PubScreen,(ULONG)A.screen,WA_Activate,TRUE,WA_DragBar,TRUE,WA_CloseGadget,TRUE,WA_DepthGadget,TRUE,WA_SizeGadget,TRUE,
  WA_InnerWidth,720,WA_InnerHeight,440,WINDOW_Position,WPOS_CENTERSCREEN,WINDOW_NewMenu,(ULONG)menus,
  A.appport?WINDOW_AppPort:TAG_IGNORE,(ULONG)A.appport,WINDOW_AppWindow,A.appport!=NULL,
  WA_IDCMP,IDCMP_CLOSEWINDOW|IDCMP_GADGETUP|IDCMP_NEWSIZE|IDCMP_REFRESHWINDOW|IDCMP_IDCMPUPDATE|IDCMP_INTUITICKS|IDCMP_RAWKEY|IDCMP_MENUPICK,
  WINDOW_IDCMPHook,(ULONG)&A.idcmp_hook,WINDOW_IDCMPHookBits,IDCMP_IDCMPUPDATE|IDCMP_REFRESHWINDOW|IDCMP_INTUITICKS,
  WINDOW_Layout,(ULONG)(NewObject(LAYOUT_GetClass(),NULL,LAYOUT_Orientation,LAYOUT_ORIENT_VERT,LAYOUT_SpaceOuter,TRUE,LAYOUT_DeferLayout,TRUE,
   LAYOUT_AddChild,(ULONG)(NewObject(LAYOUT_GetClass(),NULL,LAYOUT_Orientation,LAYOUT_ORIENT_HORIZ,
    LAYOUT_AddChild,(ULONG)make_button("Open...",B_OPEN),CHILD_WeightedWidth,0,
    LAYOUT_AddChild,(ULONG)make_button("Print...",B_PRINTDLG),CHILD_WeightedWidth,0,
    LAYOUT_AddChild,(ULONG)make_button("Save as PDF...",B_EXPORT),CHILD_WeightedWidth,0,
    LAYOUT_AddChild,(ULONG)NewObject(SPACE_GetClass(),NULL,TAG_DONE),
    LAYOUT_AddChild,(ULONG)(A.pagebutton=make_chooser(B_VIEW,view_labels,A.page?0:1)),CHILD_Label,(ULONG)make_label("Show"),CHILD_WeightedWidth,0,
    LAYOUT_AddChild,(ULONG)zoom,CHILD_WeightedWidth,0,
   TAG_DONE)),CHILD_WeightedHeight,0,
   LAYOUT_AddChild,(ULONG)(NewObject(LAYOUT_GetClass(),NULL,LAYOUT_Orientation,LAYOUT_ORIENT_HORIZ,
    LAYOUT_AddChild,(ULONG)(NewObject(LAYOUT_GetClass(),NULL,LAYOUT_Orientation,LAYOUT_ORIENT_VERT,
     LAYOUT_AddChild,(ULONG)(NewObject(LAYOUT_GetClass(),NULL,LAYOUT_Orientation,LAYOUT_ORIENT_HORIZ,
      LAYOUT_AddChild,(ULONG)(A.space=NewObject(SPACE_GetClass(),NULL,SPACE_MinWidth,280,SPACE_MinHeight,180,SPACE_RenderHook,(ULONG)&A.render_hook,TAG_DONE)),
      LAYOUT_AddChild,(ULONG)(A.vscroll=NewObject(SCROLLER_GetClass(),NULL,GA_ID,B_VSCROLL,GA_RelVerify,TRUE,SCROLLER_Orientation,SORIENT_VERT,SCROLLER_Total,1,SCROLLER_Visible,1,TAG_DONE)),CHILD_WeightedWidth,0,
     TAG_DONE)),
     LAYOUT_AddChild,(ULONG)(A.hscroll=NewObject(SCROLLER_GetClass(),NULL,GA_ID,B_HSCROLL,GA_RelVerify,TRUE,SCROLLER_Orientation,SORIENT_HORIZ,SCROLLER_Total,1,SCROLLER_Visible,1,TAG_DONE)),CHILD_WeightedHeight,0,
    TAG_DONE)),
    LAYOUT_AddChild,(ULONG)(NewObject(LAYOUT_GetClass(),NULL,LAYOUT_Orientation,LAYOUT_ORIENT_VERT,
     LAYOUT_AddChild,(ULONG)setup,CHILD_WeightedHeight,0,
     LAYOUT_AddChild,(ULONG)NewObject(SPACE_GetClass(),NULL,TAG_DONE),
     LAYOUT_AddChild,(ULONG)make_button("Printers and queue...",B_PRINTERS),CHILD_WeightedHeight,0,
     LAYOUT_AddChild,(ULONG)(A.cancel=make_button("Stop preparing",B_CANCEL)),CHILD_WeightedHeight,0,
    TAG_DONE)),CHILD_WeightedWidth,0,
   TAG_DONE)),
   LAYOUT_AddChild,(ULONG)A.status,CHILD_WeightedHeight,0,
  TAG_DONE)),
 TAG_DONE);
 if(!A.winobj)return 0;
 SetAttrs(A.cancel,GA_Disabled,TRUE,TAG_DONE);
 A.win=RA_OpenWindow(A.winobj);return A.win!=NULL;
}
static int viewer_main(int argc,char **argv)
{
 ULONG winsig=0,rexxsig=0,appsig=0,sig,res;UWORD code;char initial[OAV_PATH_MAX]={0};int rc=20;
 memset(&A,0,sizeof(A));NewList(&A.qlist);oav_layout_defaults(&A.settings);A.page=1;
 copystr(A.message,sizeof(A.message),"Open a picture or a PDF, or drop one on this window");
 if(!libraries()){fputs("OpenAmigaView: missing ReAction or datatype classes (requires AmigaOS 3.2 class set)\n",stderr);goto out;}
 if(!window_create())goto out;oap_selection_open(&A.selection);A.running=1;scan_queue();
 if(argc>1)copystr(initial,sizeof(initial),argv[1]);
 else if(argc==0){struct WBStartup *w=(struct WBStartup *)argv;if(w->sm_NumArgs>1){NameFromLock(w->sm_ArgList[1].wa_Lock,(STRPTR)initial,sizeof(initial));AddPart((STRPTR)initial,w->sm_ArgList[1].wa_Name,sizeof(initial));}}
 if(initial[0])load_file(initial);
 GetAttr(WINDOW_SigMask,A.winobj,&winsig);GetAttr(AREXX_SigMask,A.rx,&rexxsig);
 if(A.appport)appsig=1UL<<A.appport->mp_SigBit;
 while(A.running){
  sig=Wait(winsig|rexxsig|appsig|oap_selection_mask(&A.selection)|SIGBREAKF_CTRL_C);
  if(sig&appsig){struct AppMessage *am;char dropped[OAV_PATH_MAX];dropped[0]=0;
   while((am=(struct AppMessage *)GetMsg(A.appport))){
    if(am->am_NumArgs>0&&!dropped[0]&&NameFromLock(am->am_ArgList[0].wa_Lock,(STRPTR)dropped,sizeof(dropped)))AddPart((STRPTR)dropped,am->am_ArgList[0].wa_Name,sizeof(dropped));
    ReplyMsg((struct Message *)am);}
   if(dropped[0])load_file(dropped);}
  {char chosen[384];if(oap_selection_receive(&A.selection,chosen,sizeof(chosen))){
    copystr(A.printer_saved,sizeof(A.printer_saved),chosen);
  }}if(sig&SIGBREAKF_CTRL_C)A.running=0;
  if(sig&rexxsig)RA_HandleRexx(A.rx);
  while((res=RA_HandleInput(A.winobj,&code))!=WMHI_LASTMSG){
   switch(res&WMHI_CLASSMASK){case WMHI_CLOSEWINDOW:A.running=0;break;
   case WMHI_GADGETUP:action((int)(res&WMHI_GADGETMASK),NULL);break;
   case WMHI_NEWSIZE:reload();break;
   case WMHI_MENUPICK:{struct Menu *strip=NULL;UWORD number=(UWORD)(res&WMHI_MENUMASK);GetAttr(WINDOW_MenuStrip,A.winobj,(ULONG *)&strip);
    while(strip&&number!=MENUNULL){struct MenuItem *item=ItemAddress(strip,number);if(!item)break;action((int)(ULONG)GTMENUITEM_USERDATA(item),NULL);number=item->NextSelect;}
    break;}

   default:break;
   }
  }
  if(A.poll_jobs){char selected_printer[384];A.poll_jobs=0;if(oap_selected_printer(selected_printer,sizeof(selected_printer)))copystr(A.printer_saved,sizeof(A.printer_saved),selected_printer);if(A.job_active){char st[32],msg[256];if(oav_result(A.lastreq,st,sizeof(st),msg,sizeof(msg))){if(oav_result_terminal(st)){A.job_active=0;scan_queue();
    if(A.print_after){const char *pdf=strstr(msg,"Queued: ");A.print_after=0;if(pdf&&run_program("OpenAmigaPrint",pdf+8))copystr(msg,sizeof(msg),"The Print window is open for this page");}}
    SetGadgetAttrs((struct Gadget *)A.cancel,A.win,NULL,GA_Disabled,!A.job_active,TAG_DONE);status(msg);}}}
  if(A.refresh&&A.dto){A.refresh=0;RefreshDTObjectA(A.dto,A.win,NULL,NULL);scroll_info();}
 }
 rc=0;
out:
 oap_selection_close(&A.selection);
 close_content();if(A.winobj)DisposeObject(A.winobj);A.win=NULL;
 if(A.appport){struct Message *m;while((m=GetMsg(A.appport)))ReplyMsg(m);DeleteMsgPort(A.appport);}
 if(A.rx)DisposeObject(A.rx);if(ListBrowserBase)FreeListBrowserList(&A.qlist);
 if(A.have_white&&A.screen)ReleasePen(A.screen->ViewPort.ColorMap,A.whitepen);
 if(A.drawinfo)FreeScreenDrawInfo(A.screen,A.drawinfo);if(A.screen)UnlockPubScreen(NULL,A.screen);
 #define CLOSELIB(b) if(b)CloseLibrary((struct Library *)(b))
 CLOSELIB(LabelBase);CLOSELIB(ChooserBase);CLOSELIB(ARexxBase);CLOSELIB(ScrollerBase);CLOSELIB(ListBrowserBase);CLOSELIB(SpaceBase);CLOSELIB(StringBase);CLOSELIB(ButtonBase);CLOSELIB(LayoutBase);CLOSELIB(WindowBase);
 CLOSELIB(AslBase);CLOSELIB(DataTypesBase);CLOSELIB(UtilityBase);CLOSELIB(GfxBase);CLOSELIB(IntuitionBase);
 return rc;
}
int main(int argc,char **argv){return oap_main_with_stack(viewer_main,argc,argv,65536);}
