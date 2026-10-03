/* SPDX-License-Identifier: BSD-2-Clause */
/* ReAction printer chooser. Network I/O is isolated in OAPDiscover. */
#include "oap_discovery.h"
#include "oap_selection.h"
#include <exec/types.h>
#include <exec/lists.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <intuition/intuitionbase.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/listbrowser.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/string.h>
#include <proto/listbrowser.h>
#include <reaction/reaction_macros.h>
#include <clib/alib_protos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
unsigned long __stack=65536;
struct IntuitionBase *IntuitionBase;
struct Library *WindowBase,*LayoutBase,*ButtonBase,*StringBase,*ListBrowserBase;
enum { G_LIST=1,G_REFRESH,G_FILTER,G_USE,G_CLOSE,G_QUERY };
#define REG(r,t) register t __asm(#r)
static struct Browser {
 Object *window,*list,*message,*details,*use,*filter,*refresh,*manual,*query;
 struct Window *win;struct Screen *screen;struct List rows;struct Hook hook;
 OAPDiscovery data;int map[OAP_DISC_MAX],count,done,showall,poll,ticks,running,error;
 unsigned generation;char output[160],msg[256],uri[640],selected_uri[640];uint32_t signature;
} B;
static struct ColumnInfo cols[]={{45,(STRPTR)"Printer",CIF_DRAGGABLE},{15,(STRPTR)"PDF",CIF_DRAGGABLE},{40,(STRPTR)"Status",CIF_DRAGGABLE},{-1,NULL,0}};
static void message(const char *s){strncpy(B.msg,s,sizeof(B.msg)-1);B.msg[sizeof(B.msg)-1]=0;if(B.win)SetGadgetAttrs((struct Gadget *)B.message,B.win,NULL,STRINGA_TextVal,(ULONG)B.msg,TAG_DONE);}
static ULONG hook(REG(a0,struct Hook *h),REG(a2,Object *o),REG(a1,struct IntuiMessage *m)){(void)h;(void)o;if(m->Class==IDCMP_INTUITICKS&&++B.ticks>=5){B.ticks=0;B.poll=1;}return 0;}
static OAPDiscovered *selected(void){ULONG row=~0UL;GetAttr(LISTBROWSER_Selected,B.list,&row);if(row>=(ULONG)B.count)return NULL;return &B.data.printers[B.map[row]];}
static void select_row(void){OAPDiscovered *p=selected();if(p)strcpy(B.selected_uri,p->uri);else B.selected_uri[0]=0;SetGadgetAttrs((struct Gadget *)B.use,B.win,NULL,GA_Disabled,!oap_pdf_eligible(p),TAG_DONE);SetGadgetAttrs((struct Gadget *)B.details,B.win,NULL,STRINGA_TextVal,(ULONG)(p?p->uri:"Select a verified PDF printer"),TAG_DONE);if(p&&!B.error)message(p->note);}
static void fill_rows(void){size_t i;struct Node *n;char old[640]="";LONG index=-1;strcpy(old,B.selected_uri);SetGadgetAttrs((struct Gadget *)B.list,B.win,NULL,LISTBROWSER_Labels,(ULONG)-1,TAG_DONE);while((n=RemHead(&B.rows)))FreeListBrowserNode(n);B.count=0;for(i=0;i<B.data.count;i++){OAPDiscovered *p=&B.data.printers[i];const char *state=p->caps.pdf==OAP_PDF_YES?"Confirmed":p->caps.pdf==OAP_PDF_NO?"No":"Unverified";if(!B.showall&&!oap_pdf_eligible(p))continue;n=AllocListBrowserNode(3,LBNA_Column,0,LBNCA_Text,(ULONG)p->label,LBNCA_CopyText,TRUE,LBNA_Column,1,LBNCA_Text,(ULONG)state,LBNCA_CopyText,TRUE,LBNA_Column,2,LBNCA_Text,(ULONG)p->note,LBNCA_CopyText,TRUE,TAG_DONE);if(!n)break;AddTail(&B.rows,n);B.map[B.count]=(int)i;if(old[0]&&!strcmp(p->uri,old))index=B.count;B.count++;}SetGadgetAttrs((struct Gadget *)B.list,B.win,NULL,LISTBROWSER_Labels,(ULONG)&B.rows,LISTBROWSER_Selected,index,TAG_DONE);select_row();}
static uint32_t sig_data(void){uint32_t h=2166136261U;const unsigned char *p=(const unsigned char *)&B.data;size_t i;for(i=0;i<sizeof(B.data);i++)h=(h^p[i])*16777619U;return h;}
static void poll(void){char note[256];uint32_t sig;int done;if(!oap_discovery_load(B.output,&B.data,&done,note,sizeof(note)))return;B.done=done;sig=sig_data();if(sig!=B.signature){B.signature=sig;fill_rows();}if(!B.error)message(note);SetGadgetAttrs((struct Gadget *)B.refresh,B.win,NULL,GA_Disabled,!B.done,TAG_DONE);SetGadgetAttrs((struct Gadget *)B.query,B.win,NULL,GA_Disabled,!B.done,TAG_DONE);}
static int safe_uri(const char *s){size_t i,n=strlen(s);if(!n||n>=384)return 0;for(i=0;i<n;i++)if((unsigned char)s[i]<33||s[i]=='"'||s[i]=='*'||s[i]=='\'')return 0;return !strncmp(s,"ipp://",6)||!strncmp(s,"ipps://",7);}
static int begin_scan(const char *uri){char cmd[1024];BPTR input,output;LONG rc;B.error=0;if(uri&&!safe_uri(uri)){message("Enter an ipp://host:port/path URI without spaces or shell characters");return 0;}snprintf(B.output,sizeof(B.output),"T:OAPBrowse-%08lx-%u.tsv",(unsigned long)FindTask(NULL),++B.generation);if(uri)snprintf(cmd,sizeof(cmd),"C:OAPDiscover --query \"%s\" \"%s\"",uri,B.output);else snprintf(cmd,sizeof(cmd),"C:OAPDiscover \"%s\"",B.output);input=Open((STRPTR)"NIL:",MODE_OLDFILE);output=Open((STRPTR)"NIL:",MODE_NEWFILE);if(!input||!output){if(input)Close(input);if(output)Close(output);message("Cannot open worker I/O");return 0;}rc=SystemTags((STRPTR)cmd,SYS_Asynch,TRUE,SYS_Input,input,SYS_Output,output,NP_StackSize,65536,TAG_DONE);if(rc==-1){Close(input);Close(output);message("Cannot launch C:OAPDiscover");return 0;}B.done=0;B.signature=0;memset(&B.data,0,sizeof(B.data));fill_rows();SetGadgetAttrs((struct Gadget *)B.refresh,B.win,NULL,GA_Disabled,TRUE,TAG_DONE);SetGadgetAttrs((struct Gadget *)B.query,B.win,NULL,GA_Disabled,TRUE,TAG_DONE);message(uri?"Checking printer capabilities...":"Discovering IPP printers; no document is being printed");return 1;}
static int ensure_dir(const char *s){BPTR l=Lock((STRPTR)s,ACCESS_READ);if(l){UnLock(l);return 1;}l=CreateDir((STRPTR)s);if(l){UnLock(l);return 1;}return 0;}
static Object *button(const char *s,ULONG id){return NewObject(BUTTON_GetClass(),NULL,GA_Text,(ULONG)s,GA_ID,id,GA_RelVerify,TRUE,TAG_DONE);}
int main(int argc,char **argv){ULONG sig,res;WORD code;int result=20;struct Node *n;(void)argc;(void)argv;memset(&B,0,sizeof(B));NewList(&B.rows);B.done=1;
 IntuitionBase=(struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library",39);WindowBase=OpenLibrary((STRPTR)"window.class",44);LayoutBase=OpenLibrary((STRPTR)"gadgets/layout.gadget",44);ButtonBase=OpenLibrary((STRPTR)"gadgets/button.gadget",44);StringBase=OpenLibrary((STRPTR)"gadgets/string.gadget",44);ListBrowserBase=OpenLibrary((STRPTR)"gadgets/listbrowser.gadget",44);if(!IntuitionBase||!WindowBase||!LayoutBase||!ButtonBase||!StringBase||!ListBrowserBase)goto done;B.screen=LockPubScreen(NULL);if(!B.screen)goto done;B.hook.h_Entry=(ULONG (*)())hook;
 B.window=NewObject(WINDOW_GetClass(),NULL,WA_Title,(ULONG)"OpenAmigaPrint - Browse PDF Printers",WA_ScreenTitle,(ULONG)"ReAction | DNS-SD discovery + verified IPP capabilities",WA_PubScreen,(ULONG)B.screen,WA_InnerWidth,760,WA_InnerHeight,320,WA_DragBar,TRUE,WA_DepthGadget,TRUE,WA_CloseGadget,TRUE,WA_SizeGadget,TRUE,WA_Activate,TRUE,WA_IDCMP,IDCMP_INTUITICKS,WINDOW_IDCMPHook,(ULONG)&B.hook,WINDOW_IDCMPHookBits,IDCMP_INTUITICKS,WINDOW_Position,WPOS_CENTERSCREEN,
 WINDOW_Layout,(ULONG)NewObject(LAYOUT_GetClass(),NULL,LAYOUT_Orientation,LAYOUT_ORIENT_VERT,LAYOUT_SpaceOuter,TRUE,LAYOUT_DeferLayout,TRUE,
 LAYOUT_AddChild,(ULONG)NewObject(LAYOUT_GetClass(),NULL,LAYOUT_Orientation,LAYOUT_ORIENT_HORIZ,LAYOUT_AddChild,(ULONG)(B.refresh=button("Refresh",G_REFRESH)),LAYOUT_AddChild,(ULONG)(B.filter=button("Show all printers",G_FILTER)),TAG_DONE),CHILD_WeightedHeight,0,
 LAYOUT_AddChild,(ULONG)(B.list=NewObject(LISTBROWSER_GetClass(),NULL,GA_ID,G_LIST,GA_RelVerify,TRUE,LISTBROWSER_Labels,(ULONG)&B.rows,LISTBROWSER_ColumnInfo,(ULONG)cols,LISTBROWSER_ColumnTitles,TRUE,LISTBROWSER_AutoFit,TRUE,LISTBROWSER_MinVisible,7,TAG_DONE)),
 LAYOUT_AddChild,(ULONG)(B.details=NewObject(STRING_GetClass(),NULL,GA_ReadOnly,TRUE,STRINGA_MaxChars,640,STRINGA_MinVisible,35,STRINGA_TextVal,(ULONG)"Select a verified PDF printer",TAG_DONE)),CHILD_WeightedHeight,0,
 LAYOUT_AddChild,(ULONG)NewObject(LAYOUT_GetClass(),NULL,LAYOUT_Orientation,LAYOUT_ORIENT_HORIZ,LAYOUT_AddChild,(ULONG)(B.manual=NewObject(STRING_GetClass(),NULL,STRINGA_MaxChars,384,STRINGA_MinVisible,30,STRINGA_TextVal,(ULONG)"ipp://",GA_RelVerify,TRUE,TAG_DONE)),LAYOUT_AddChild,(ULONG)(B.query=button("Query address",G_QUERY)),CHILD_WeightedWidth,0,TAG_DONE),CHILD_WeightedHeight,0,
 LAYOUT_AddChild,(ULONG)(B.message=NewObject(STRING_GetClass(),NULL,GA_ReadOnly,TRUE,STRINGA_MaxChars,256,STRINGA_MinVisible,45,STRINGA_TextVal,(ULONG)"Only verified PDF-capable printers can be selected",TAG_DONE)),CHILD_WeightedHeight,0,
 LAYOUT_AddChild,(ULONG)NewObject(LAYOUT_GetClass(),NULL,LAYOUT_Orientation,LAYOUT_ORIENT_HORIZ,LAYOUT_AddChild,(ULONG)(B.use=button("Use Printer",G_USE)),LAYOUT_AddChild,(ULONG)button("Close",G_CLOSE),TAG_DONE),CHILD_WeightedHeight,0,TAG_DONE),TAG_DONE);
 if(!B.window)goto done;B.win=RA_OpenWindow(B.window);if(!B.win)goto done;B.running=1;SetGadgetAttrs((struct Gadget *)B.use,B.win,NULL,GA_Disabled,TRUE,TAG_DONE);begin_scan(NULL);
 while(B.running){GetAttr(WINDOW_SigMask,B.window,&sig);if(Wait(sig|SIGBREAKF_CTRL_C)&SIGBREAKF_CTRL_C)B.running=0;while((res=RA_HandleInput(B.window,&code))!=WMHI_LASTMSG){switch(res&WMHI_CLASSMASK){case WMHI_CLOSEWINDOW:B.running=0;break;case WMHI_GADGETUP:switch(res&WMHI_GADGETMASK){case G_CLOSE:B.running=0;break;case G_LIST:B.error=0;select_row();break;case G_REFRESH:if(B.done)begin_scan(NULL);break;case G_FILTER:B.showall=!B.showall;SetGadgetAttrs((struct Gadget *)B.filter,B.win,NULL,GA_Text,(ULONG)(B.showall?"PDF printers only":"Show all printers"),TAG_DONE);fill_rows();break;case G_QUERY:if(B.done){ULONG v=0;GetAttr(STRINGA_TextVal,B.manual,&v);if(v)begin_scan((char *)v);}break;case G_USE:{OAPDiscovered *p=selected();if(!oap_pdf_eligible(p)){message("PDF support is not verified; selection refused");break;}if(strlen(p->uri)>=384){message("Endpoint exceeds the current print transport limit");break;}if(!ensure_dir("ENV:OpenAmigaPrint")||!ensure_dir("ENVARC:OpenAmigaPrint")){B.error=1;message("Cannot create printer preference drawers");break;}{char error[256];if(!oap_preferences_store("OpenAmigaPrint/PrinterURI",p->uri,error,sizeof(error))){B.error=1;message(error);break;}}if(oap_selection_publish(p->uri)<0){B.error=1;message("Printer saved; low memory prevented window notification. Try Use Printer again.");break;}result=0;B.running=0;break;}}break;}}
 if(B.poll){B.poll=0;poll();}
 }
 if(result==20)result=5;
done:if(B.window){DisposeObject(B.window);B.win=NULL;}while((n=RemHead(&B.rows)))FreeListBrowserNode(n);if(B.screen)UnlockPubScreen(NULL,B.screen);
#define CLOSELIB(x) if(x)CloseLibrary((struct Library *)x)
 CLOSELIB(ListBrowserBase);CLOSELIB(StringBase);CLOSELIB(ButtonBase);CLOSELIB(LayoutBase);CLOSELIB(WindowBase);CLOSELIB(IntuitionBase);
 return result;}
