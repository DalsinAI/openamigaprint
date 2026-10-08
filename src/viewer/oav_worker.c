/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#include "oap_stack.h"
#include "oav_core.h"
#include "oav_jobs.h"
#include "oap.h"
#include "oap_queue.h"
#include "oap_str.h"
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuitionbase.h>
#include <graphics/gfxbase.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/datatypes.h>
#include <clib/alib_protos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
unsigned long __stack = 65536;
static const char oap_version[] __attribute__((used)) = "$VER: OAVWorker 0.3 (4.10.2026)";
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *DataTypesBase;
typedef struct Pixels{Object *dto;unsigned long width;unsigned char *argb;char cancel[OAV_PATH_MAX+16];int cancelled;}Pixels;
static int is_cancelled(Pixels *p)
{BPTR l;if(SetSignal(0,0)&SIGBREAKF_CTRL_C)return 1;l=Lock((STRPTR)p->cancel,ACCESS_READ);if(l){UnLock(l);return 1;}return 0;}
static int row(void *ctx,unsigned long y,unsigned char *rgb,size_t bytes)
{
 Pixels *p=ctx;struct pdtBlitPixelArray r;unsigned long x;
 if((y%16)==0&&is_cancelled(p)){p->cancelled=1;return 0;}
 memset(&r,0,sizeof(r));r.MethodID=PDTM_READPIXELARRAY;r.pbpa_Left=0;r.pbpa_Top=y;r.pbpa_Width=p->width;r.pbpa_Height=1;
 if(p->argb){
  r.pbpa_PixelData=p->argb;r.pbpa_PixelFormat=PBPAFMT_ARGB;r.pbpa_PixelArrayMod=p->width*4;
  if(!DoMethodA(p->dto,(Msg)&r))return 0;
  for(x=0;x<p->width;x++){unsigned long a=p->argb[x*4],c;for(c=0;c<3;c++)rgb[x*3+c]=(unsigned char)((p->argb[x*4+1+c]*a+255*(255-a)+127)/255);}
  return 1;
 }
 r.pbpa_PixelData=rgb;r.pbpa_PixelFormat=PBPAFMT_RGB;r.pbpa_PixelArrayMod=bytes;
 return DoMethodA(p->dto,(Msg)&r)!=0;
}
static void result(const char *req,const char *state,const char *msg)
{
 char path[OAV_PATH_MAX+20],temp[OAV_PATH_MAX+24];FILE *f;
 snprintf(path,sizeof(path),"%s.result",req);snprintf(temp,sizeof(temp),"%s.tmp",path);
 f=fopen(temp,"w");if(!f)return;fprintf(f,"state=%s\nmessage=%s\n",state,msg);if(fclose(f)){remove(temp);return;}remove(path);rename(temp,path);
}
typedef struct PrintProgress {
 const char *request,*source,*uri;
 Pixels cancel;
 unsigned long last_bytes;
 char stage[32];
} PrintProgress;
static int print_progress(void *opaque,const char *stage,unsigned long sent,unsigned long total)
{
 PrintProgress *p=opaque;char text[240];int changed=strcmp(stage,p->stage)!=0;
 if(is_cancelled(&p->cancel))return 0;
 if(!changed&&sent<p->last_bytes+524288UL&&sent!=total)return 1;
 if(!strcmp(stage,"uploading"))snprintf(text,sizeof(text),"Uploading %lu%% (%lu / %lu KiB)",total?(sent==total?100UL:sent/(total/100UL+1)):0UL,sent/1024UL,total/1024UL);
 else snprintf(text,sizeof(text),"%s",!strcmp(stage,"checking")?"Checking printer PDF capability":!strcmp(stage,"connecting")?"Connecting to selected printer":!strcmp(stage,"awaiting-reply")?"Upload complete; waiting for printer job ID":"Preparing PDF submission");
 result(p->request,stage,text);
 if(changed)oav_update_queue_state(p->source,stage,p->uri,text);
 oap_copy(p->stage,sizeof(p->stage),stage);p->last_bytes=sent;return 1;
}
static int copyfile(const char *from,const char *to)
{
 FILE *a=fopen(from,"rb"),*b;static char buf[8192];size_t n;unsigned long total=0;int ok=1;
 if(!a)return 0;
 b=fopen(to,"wb");if(!b){fclose(a);return 0;}
 while((n=fread(buf,1,sizeof(buf),a))){total+=(unsigned long)n;if(total>OAV_FILE_LIMIT||fwrite(buf,1,n,b)!=n){ok=0;break;}}
 if(ferror(a))ok=0;
 fclose(a);
 if(fclose(b))ok=0;
 if(!ok)remove(to);
 return ok;
}
static int worker_main(int argc,char **argv)
{
 static OAVRequest r;static Pixels px;Object *dto=NULL;struct BitMapHeader *bmh=NULL;FILE *f=NULL;
 static char snapshot[OAV_PATH_MAX+20],out[OAV_PATH_MAX],part[OAV_PATH_MAX+20],meta[OAV_PATH_MAX+20],msg[256];
 char hdr[6]={0};const char *id;long size;ULONG alpha=0;int ok=0;BPTR l;
 if(argc!=2)return 20;
 memset(&px,0,sizeof(px));part[0]=0;
 {char tracepath[OAV_PATH_MAX+16];FILE *trace;struct Task *task=FindTask(NULL);
  snprintf(tracepath,sizeof(tracepath),"%s.trace",argv[1]);trace=fopen(tracepath,"w");
  if(trace){fprintf(trace,"worker started; stack bytes=%lu\n",(unsigned long)((UBYTE *)task->tc_SPUpper-(UBYTE *)task->tc_SPLower));fclose(trace);}}

 if(!oav_read_request(argv[1],&r)){result(argv[1],"error","Invalid job request");return 20;}
 snprintf(px.cancel,sizeof(px.cancel),"%s.cancel",argv[1]);
 if(!strcmp(r.action,"send")){
  static OAPJobOptions o;static PrintProgress progress;
  const char *state;int rc;
  oap_job_defaults(&o);if(r.has_print_options)o=r.print;
  oap_copy(o.printer_uri,sizeof(o.printer_uri),r.uri);
  memset(&progress,0,sizeof(progress));progress.request=argv[1];progress.source=r.source;progress.uri=r.uri;
  snprintf(progress.cancel.cancel,sizeof(progress.cancel.cancel),"%s.cancel",argv[1]);
  rc=oap_ipp_submit_pdf_ex(r.source,&o,msg,sizeof(msg),print_progress,&progress);
  state=rc==OAP_SEND_ACCEPTED?"submitted":rc==OAP_SEND_UNCERTAIN?"uncertain":rc==OAP_SEND_CANCELLED?"cancelled":"error";
  oav_update_queue_state(r.source,state,r.uri,msg);result(argv[1],state,msg);
  return rc==OAP_SEND_ACCEPTED?0:rc==OAP_SEND_CANCELLED?5:20;
 }
 snprintf(snapshot,sizeof(snapshot),"%s.source",argv[1]);
 if(!copyfile(r.source,snapshot)){result(argv[1],"error","Cannot snapshot source, or file exceeds 64 MiB limit");return 20;}
 id=strrchr(argv[1],'/');id=id?id+1:argv[1];
 if(!strcmp(r.action,"queue"))snprintf(out,sizeof(out),OAP_QUEUE_DIR "/%s.pdf",id);
 else if(!strcmp(r.action,"export")&&oav_safe_field(r.output))oap_copy(out,sizeof(out),r.output);
 else {result(argv[1],"error","Unsupported job action");return 20;}
 l=Lock((STRPTR)out,ACCESS_READ);if(l){UnLock(l);result(argv[1],"error","Output already exists; choose a new filename (source preserved)");return 20;}
 snprintf(part,sizeof(part),"%s.part",out);l=Lock((STRPTR)part,ACCESS_READ);if(l){UnLock(l);result(argv[1],"error","An unfinished output already exists; choose another filename");return 20;}
 f=fopen(snapshot,"rb");if(!f){strcpy(msg,"Cannot read source snapshot");goto done;}if(fread(hdr,1,5,f)!=5)hdr[0]=0;
 fclose(f);f=NULL;
 if(!memcmp(hdr,"%PDF-",5)){ok=copyfile(snapshot,part);strcpy(msg,ok?"PDF copied unchanged (layout not reapplied)":"PDF copy failed");goto commit;}
 IntuitionBase=(struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library",39);
 GfxBase=(struct GfxBase *)OpenLibrary((STRPTR)"graphics.library",39);DataTypesBase=OpenLibrary((STRPTR)"datatypes.library",44);
 if(!IntuitionBase||!GfxBase||!DataTypesBase){strcpy(msg,"Required datatype/graphics libraries are missing");goto done;}
 dto=NewDTObject((APTR)snapshot,DTA_SourceType,DTST_FILE,DTA_GroupID,GID_PICTURE,PDTA_DestMode,PMODE_V43,PDTA_Remap,FALSE,TAG_DONE);
 if(!dto){snprintf(msg,sizeof(msg),"No picture decoder for this source: %.180s",oav_format_note(r.source));goto done;}
 GetDTAttrs(dto,PDTA_BitMapHeader,(ULONG)&bmh,PDTA_AlphaChannel,(ULONG)&alpha,TAG_DONE);
 if(!bmh||!bmh->bmh_Width||!bmh->bmh_Height||(unsigned long)bmh->bmh_Width>OAV_PIXEL_LIMIT/bmh->bmh_Height){strcpy(msg,"Image exceeds the 16 megapixel render limit");goto done;}
 px.dto=dto;px.width=bmh->bmh_Width;
 if(alpha){px.argb=malloc(px.width*4);if(!px.argb){strcpy(msg,"Cannot allocate alpha-composite row");goto done;}}
 if(is_cancelled(&px)){px.cancelled=1;strcpy(msg,"Cancelled before render");goto done;}
 f=fopen(part,"wb");if(!f){strcpy(msg,"Cannot create output");goto done;}
 ok=oav_pdf_rgb(f,bmh->bmh_Width,bmh->bmh_Height,&r.layout,row,&px,msg,sizeof(msg));
 if(fclose(f)){ok=0;strcpy(msg,"Output close failed");}f=NULL;
commit:
 if(ok&&rename(part,out)){ok=0;strcpy(msg,"Cannot commit completed PDF");}
 if(ok&&!strcmp(r.action,"queue")){
  size_t n;oap_copy(meta,sizeof(meta),out);n=strlen(meta);strcpy(meta+n-4,".job");
  f=fopen(out,"rb");size=0;if(f){fseek(f,0,SEEK_END);size=ftell(f);fclose(f);f=NULL;}
  f=fopen(meta,"w");if(f){fprintf(f,"state=queued\nbytes=%ld\npdf=%s\nsource=%s\nrequest=%s\ntitle=%s\npaper=%d\nlandscape=%d\n",size,out,snapshot,argv[1],FilePart((STRPTR)r.source),r.layout.paper,r.layout.landscape);if(fclose(f)){ok=0;strcpy(msg,"PDF saved but queue metadata close failed");}f=NULL;}else{ok=0;strcpy(msg,"PDF saved but queue metadata could not be created");}
 }
 if(ok)snprintf(msg,sizeof(msg),"%s: %.210s",!strcmp(r.action,"queue")?"Queued":"Saved",out);
done:
 if(f)fclose(f);
 if(dto)DisposeDTObject(dto);
 free(px.argb);
 if(DataTypesBase)CloseLibrary(DataTypesBase);
 if(GfxBase)CloseLibrary((struct Library *)GfxBase);
 if(IntuitionBase)CloseLibrary((struct Library *)IntuitionBase);
 if(!ok&&part[0])remove(part);
 result(argv[1],ok?"ready":px.cancelled?"cancelled":"error",msg);return ok?0:20;
}
int main(int argc,char **argv){return oap_main_with_stack(worker_main,argc,argv,65536);}
