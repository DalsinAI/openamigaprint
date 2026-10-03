/* SPDX-License-Identifier: BSD-2-Clause */
#include "oav_core.h"
#include "oav_jobs.h"
#include "oap.h"
#include "oap_queue.h"
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
  if(!DoDTMethodA(p->dto,NULL,NULL,(Msg)&r))return 0;
  for(x=0;x<p->width;x++){unsigned long a=p->argb[x*4],c;for(c=0;c<3;c++)rgb[x*3+c]=(unsigned char)((p->argb[x*4+1+c]*a+255*(255-a)+127)/255);}
  return 1;
 }
 r.pbpa_PixelData=rgb;r.pbpa_PixelFormat=PBPAFMT_RGB;r.pbpa_PixelArrayMod=bytes;
 return DoDTMethodA(p->dto,NULL,NULL,(Msg)&r)!=0;
}
static void result(const char *req,const char *state,const char *msg)
{
 char path[OAV_PATH_MAX+20],temp[OAV_PATH_MAX+24];FILE *f;
 snprintf(path,sizeof(path),"%s.result",req);snprintf(temp,sizeof(temp),"%s.tmp",path);
 f=fopen(temp,"w");if(!f)return;fprintf(f,"state=%s\nmessage=%s\n",state,msg);if(fclose(f)){remove(temp);return;}rename(temp,path);
}
static int copyfile(const char *from,const char *to)
{
 FILE *a=fopen(from,"rb"),*b;char buf[8192];size_t n;unsigned long total=0;int ok=1;
 if(!a)return 0;b=fopen(to,"wb");if(!b){fclose(a);return 0;}
 while((n=fread(buf,1,sizeof(buf),a))){total+=(unsigned long)n;if(total>OAV_FILE_LIMIT||fwrite(buf,1,n,b)!=n){ok=0;break;}}
 if(ferror(a))ok=0;fclose(a);if(fclose(b))ok=0;if(!ok)remove(to);return ok;
}
int main(int argc,char **argv)
{
 OAVRequest r;Pixels px;Object *dto=NULL;struct BitMapHeader *bmh=NULL;FILE *f=NULL;
 char snapshot[OAV_PATH_MAX+20],out[OAV_PATH_MAX],part[OAV_PATH_MAX+20],meta[OAV_PATH_MAX+20],msg[256];
 char hdr[6]={0};const char *id;long size;ULONG alpha=0;int ok=0;BPTR l;
 if(argc!=2)return 20;memset(&px,0,sizeof(px));part[0]=0;
 if(!oav_read_request(argv[1],&r)){result(argv[1],"error","Invalid job request");return 20;}
 snprintf(px.cancel,sizeof(px.cancel),"%s.cancel",argv[1]);
 if(!strcmp(r.action,"send")){
  OAPJobOptions o;OAPUri u;oap_job_defaults(&o);
  if(!oap_parse_ipp_uri(r.uri,&u)){result(argv[1],"error","Invalid IPP URI; IPPS not enabled in this build");return 20;}
  strncpy(o.printer_uri,r.uri,sizeof(o.printer_uri)-1);
  ok=oap_ipp_submit_pdf(r.source,&o,msg,sizeof(msg));
  result(argv[1],ok?"submitted":"error",ok?"IPP accepted submission; physical completion is not yet monitored":msg);return ok?0:20;
 }
 snprintf(snapshot,sizeof(snapshot),"%s.source",argv[1]);
 if(!copyfile(r.source,snapshot)){result(argv[1],"error","Cannot snapshot source, or file exceeds 64 MiB limit");return 20;}
 id=strrchr(argv[1],'/');id=id?id+1:argv[1];
 if(!strcmp(r.action,"queue"))snprintf(out,sizeof(out),OAP_QUEUE_DIR "/%s.pdf",id);
 else if(!strcmp(r.action,"export")&&oav_safe_field(r.output)){strncpy(out,r.output,sizeof(out)-1);out[sizeof(out)-1]=0;}
 else {result(argv[1],"error","Unsupported job action");return 20;}
 l=Lock((STRPTR)out,ACCESS_READ);if(l){UnLock(l);result(argv[1],"error","Output already exists; choose a new filename (source preserved)");return 20;}
 snprintf(part,sizeof(part),"%s.part",out);l=Lock((STRPTR)part,ACCESS_READ);if(l){UnLock(l);result(argv[1],"error","An unfinished output already exists; choose another filename");return 20;}
 f=fopen(snapshot,"rb");if(!f){strcpy(msg,"Cannot read source snapshot");goto done;}fread(hdr,1,5,f);fclose(f);f=NULL;
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
  size_t n;strncpy(meta,out,sizeof(meta)-1);meta[sizeof(meta)-1]=0;n=strlen(meta);strcpy(meta+n-4,".job");
  f=fopen(out,"rb");size=0;if(f){fseek(f,0,SEEK_END);size=ftell(f);fclose(f);f=NULL;}
  f=fopen(meta,"w");if(f){fprintf(f,"state=queued\nbytes=%ld\npdf=%s\nsource=%s\nrequest=%s\n",size,out,snapshot,argv[1]);if(fclose(f)){ok=0;strcpy(msg,"PDF saved but queue metadata close failed");}f=NULL;}else{ok=0;strcpy(msg,"PDF saved but queue metadata could not be created");}
 }
 if(ok)snprintf(msg,sizeof(msg),"%s: %.210s",!strcmp(r.action,"queue")?"Queued":"Saved",out);
done:
 if(f)fclose(f);if(dto)DisposeDTObject(dto);free(px.argb);
 if(DataTypesBase)CloseLibrary(DataTypesBase);if(GfxBase)CloseLibrary((struct Library *)GfxBase);if(IntuitionBase)CloseLibrary((struct Library *)IntuitionBase);
 if(!ok&&part[0])remove(part);
 result(argv[1],ok?"ready":px.cancelled?"cancelled":"error",msg);return ok?0:20;
}
