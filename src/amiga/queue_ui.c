#include "oap.h"
#include "oap_queue.h"
#include <exec/types.h>
#include <exec/lists.h>
#include <intuition/intuitionbase.h>
#include <graphics/gfxbase.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define OAP_QUEUE_MAX 32
#define OAP_PATH_MAX 160

enum { Q_LIST=100,Q_OPEN,Q_DELETE,Q_REFRESH,Q_CLOSE };

static struct List q_list;
static struct Node q_nodes[OAP_QUEUE_MAX];
static char q_names[OAP_QUEUE_MAX][72];
static char q_paths[OAP_QUEUE_MAX][OAP_PATH_MAX];
static int q_count;

static int has_pdf_suffix(const char *s)
{
    size_t n=s?strlen(s):0;
    return n>4 && !strcmp(s+n-4,".pdf");
}
static long queue_file_size(const char *path)
{
    FILE *f=fopen(path,"rb");long n=-1;
    if(!f)return -1;
    if(!fseek(f,0,SEEK_END))n=ftell(f);
    fclose(f);
    return n;
}
static void queue_state(const char *pdf,char *state,size_t cap)
{
    char path[OAP_PATH_MAX],line[96];FILE *f;size_t n;strncpy(state,"queued",cap-1);state[cap-1]=0;
    strncpy(path,pdf,sizeof(path)-1);path[sizeof(path)-1]=0;n=strlen(path);if(n<4)return;strcpy(path+n-4,".job");
    f=fopen(path,"r");if(!f)return;while(fgets(line,sizeof(line),f)){if(!strncmp(line,"state=",6)){char *e;strncpy(state,line+6,cap-1);state[cap-1]=0;e=strchr(state,'\n');if(e)*e=0;break;}}fclose(f);
}
static void queue_label(char *dst,size_t cap,const char *file,const char *path)
{
    char base[40],state[20];long bytes=queue_file_size(path);size_t n=strlen(file);
    if(n>4)n-=4;
    if(n>=sizeof(base))n=sizeof(base)-1;
    memcpy(base,file,n);base[n]=0;
    queue_state(path,state,sizeof(state));
    if(bytes>=1024L*1024L)snprintf(dst,cap,"%-12s %-9s %ld.%ld MB",base,state,bytes/(1024L*1024L),(bytes%(1024L*1024L))*10/(1024L*1024L));
    else if(bytes>=1024)snprintf(dst,cap,"%-12s %-9s %ld KB",base,state,bytes/1024);
    else snprintf(dst,cap,"%-12s %-9s %ld B",base,state,bytes<0?0:bytes);
}
static void clear_queue(void)
{
    int i;
    q_list.lh_Head=(struct Node *)&q_list.lh_Tail;q_list.lh_Tail=NULL;
    q_list.lh_TailPred=(struct Node *)&q_list.lh_Head;q_list.lh_Type=0;q_list.l_pad=0;q_count=0;
    for(i=0;i<OAP_QUEUE_MAX;i++){memset(&q_nodes[i],0,sizeof(q_nodes[i]));q_names[i][0]=0;q_paths[i][0]=0;}
}
static void scan_queue(void)
{
    DIR *d;struct dirent *e;
    clear_queue();d=opendir(OAP_QUEUE_DIR);if(!d)return;
    while(q_count<OAP_QUEUE_MAX && (e=readdir(d))!=NULL){
        if(!has_pdf_suffix(e->d_name))continue;
        snprintf(q_paths[q_count],sizeof(q_paths[q_count]),"%s/%s",OAP_QUEUE_DIR,e->d_name);
        queue_label(q_names[q_count],sizeof(q_names[q_count]),e->d_name,q_paths[q_count]);
        q_nodes[q_count].ln_Name=q_names[q_count];AddTail(&q_list,&q_nodes[q_count]);q_count++;
    }
    closedir(d);
}
static int selected_index(struct Gadget *g,struct Window *w)
{
    ULONG v=0;GT_GetGadgetAttrs(g,w,NULL,GTLV_Selected,(ULONG)&v,TAG_END);
    return v<((ULONG)q_count)?(int)v:-1;
}
static void launch_path(const char *path)
{
    char cmd[240];if(!path||!path[0])return;
    snprintf(cmd,sizeof(cmd),"Run >NIL: C:OpenAmigaPrint \"%s\"",path);system(cmd);
}
static void delete_path(const char *path)
{
    char meta[OAP_PATH_MAX];size_t n;if(!path||!path[0])return;
    remove(path);strncpy(meta,path,sizeof(meta)-1);meta[sizeof(meta)-1]=0;n=strlen(meta);
    if(n>4){strcpy(meta+n-4,".job");remove(meta);}
}
static void refresh_list(struct Gadget *g,struct Window *w)
{
    GT_SetGadgetAttrs(g,w,NULL,GTLV_Labels,(ULONG)-1,TAG_END);scan_queue();
    GT_SetGadgetAttrs(g,w,NULL,GTLV_Labels,(ULONG)&q_list,GTLV_Selected,0,TAG_END);
}
int oap_run_queue_window(void)
{
    struct Library *gt=NULL;struct IntuitionBase *ib=NULL;struct GfxBase *gb=NULL;
    struct Screen *scr=NULL;APTR vi=NULL;struct Gadget *list=NULL,*last,*glist,*gopen,*gdelete,*grefresh,*gclose;
    struct NewGadget ng;struct TextAttr ta={(STRPTR)"topaz.font",8,0,0};struct Window *w=NULL;int done=0,ret=1;
    ib=(struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library",39);gb=(struct GfxBase *)OpenLibrary((STRPTR)"graphics.library",39);gt=OpenLibrary((STRPTR)"gadtools.library",39);
    if(!ib||!gb||!gt){ret=0;goto out;}IntuitionBase=ib;GfxBase=gb;GadToolsBase=gt;
    scr=LockPubScreen(NULL);if(!scr){ret=0;goto out;}vi=GetVisualInfoA(scr,NULL);if(!vi){ret=0;goto out;}
    scan_queue();memset(&ng,0,sizeof(ng));ng.ng_TextAttr=&ta;ng.ng_VisualInfo=vi;last=CreateContext(&list);
    ng.ng_GadgetID=Q_LIST;ng.ng_LeftEdge=12;ng.ng_TopEdge=14;ng.ng_Width=476;ng.ng_Height=152;ng.ng_GadgetText=NULL;
    last=glist=CreateGadget(LISTVIEW_KIND,last,&ng,GTLV_Labels,(ULONG)&q_list,GTLV_Selected,0,GTLV_ShowSelected,(ULONG)0,TAG_END);
    ng.ng_TopEdge=180;ng.ng_Height=18;ng.ng_Width=82;
    ng.ng_LeftEdge=52;ng.ng_GadgetID=Q_OPEN;ng.ng_GadgetText=(STRPTR)"Open";last=gopen=CreateGadget(BUTTON_KIND,last,&ng,TAG_END);
    ng.ng_LeftEdge=144;ng.ng_GadgetID=Q_DELETE;ng.ng_GadgetText=(STRPTR)"Delete";last=gdelete=CreateGadget(BUTTON_KIND,last,&ng,TAG_END);
    ng.ng_LeftEdge=236;ng.ng_GadgetID=Q_REFRESH;ng.ng_GadgetText=(STRPTR)"Refresh";last=grefresh=CreateGadget(BUTTON_KIND,last,&ng,TAG_END);
    ng.ng_LeftEdge=328;ng.ng_GadgetID=Q_CLOSE;ng.ng_GadgetText=(STRPTR)"Close";last=gclose=CreateGadget(BUTTON_KIND,last,&ng,TAG_END);
    if(!glist||!gopen||!gdelete||!grefresh||!gclose){ret=0;goto out;}
    w=OpenWindowTags(NULL,WA_Title,(ULONG)"OpenAmigaPrint - Queue",WA_PubScreen,(ULONG)scr,WA_InnerWidth,500,WA_InnerHeight,212,WA_Gadgets,(ULONG)list,WA_DragBar,TRUE,WA_DepthGadget,TRUE,WA_CloseGadget,TRUE,WA_Activate,TRUE,WA_SimpleRefresh,TRUE,WA_IDCMP,IDCMP_CLOSEWINDOW|IDCMP_REFRESHWINDOW|LISTVIEWIDCMP|BUTTONIDCMP,TAG_END);
    if(!w){ret=0;goto out;}GT_RefreshWindow(w,NULL);
    while(!done){struct IntuiMessage *m;Wait(1UL<<w->UserPort->mp_SigBit);while((m=GT_GetIMsg(w->UserPort))){ULONG cls=m->Class;UWORD id=m->IAddress?((struct Gadget *)m->IAddress)->GadgetID:0;GT_ReplyIMsg(m);if(cls==IDCMP_CLOSEWINDOW){done=1;break;}if(cls==IDCMP_REFRESHWINDOW){GT_BeginRefresh(w);GT_EndRefresh(w,TRUE);}else if(cls==IDCMP_GADGETUP){int i=selected_index(glist,w);if(id==Q_CLOSE)done=1;else if(id==Q_REFRESH)refresh_list(glist,w);else if(id==Q_OPEN&&i>=0)launch_path(q_paths[i]);else if(id==Q_DELETE&&i>=0){delete_path(q_paths[i]);refresh_list(glist,w);}}}}
out:
    if(w)CloseWindow(w);if(list)FreeGadgets(list);if(vi)FreeVisualInfo(vi);if(scr)UnlockPubScreen(NULL,scr);if(gt)CloseLibrary(gt);if(gb)CloseLibrary((struct Library *)gb);if(ib)CloseLibrary((struct Library *)ib);return ret;
}
