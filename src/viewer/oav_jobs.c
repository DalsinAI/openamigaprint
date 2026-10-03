/* SPDX-License-Identifier: BSD-2-Clause */
#include "oav_jobs.h"
#include "oap_queue.h"
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
static int mkdir_amiga(const char *p){BPTR l=Lock((STRPTR)p,ACCESS_READ);if(l){UnLock(l);return 1;}l=CreateDir((STRPTR)p);if(l)UnLock(l);return l!=0;}
static void copystr(char *d,size_t n,const char *s){if(n){strncpy(d,s,n-1);d[n-1]=0;}}
int oav_submit(const OAVRequest *r,char *request,size_t cap,char *err,size_t errcap)
{
 struct DateStamp ds;static unsigned long seq;char path[256],cmd[320];FILE *f;int fd,i;LONG rc;
 if(!r||!oav_safe_field(r->source)||(strcmp(r->action,"queue")&&strcmp(r->action,"export")&&strcmp(r->action,"send"))){copystr(err,errcap,"Invalid request");return 0;}
 if(!strcmp(r->action,"export")&&!oav_safe_field(r->output)){copystr(err,errcap,"Choose an output filename");return 0;}
 if(!strcmp(r->action,"send")&&!oav_safe_field(r->uri)){copystr(err,errcap,"Set the PDF-capable IPP destination first");return 0;}
 if(!mkdir_amiga(OAP_QUEUE_ROOT)||!mkdir_amiga(OAP_QUEUE_DIR)||!mkdir_amiga(OAV_REQUEST_DIR)){copystr(err,errcap,"Cannot create spool folders");return 0;}
 DateStamp(&ds);
 for(i=0;i<100;i++){
  snprintf(path,sizeof(path),OAV_REQUEST_DIR "/v-%08lx-%08lx-%04lx.req",(unsigned long)ds.ds_Days,(unsigned long)(ds.ds_Minute*3000+ds.ds_Tick),++seq);
  fd=open(path,O_CREAT|O_EXCL|O_WRONLY,0600);if(fd>=0)break;
 }
 if(i==100){copystr(err,errcap,"Cannot allocate a unique job ID");return 0;}
 f=fdopen(fd,"w");if(!f){close(fd);remove(path);copystr(err,errcap,"Cannot write request");return 0;}
 fprintf(f,"schema=1\naction=%s\nsource=%s\noutput=%s\nuri=%s\npaper=%d\nlandscape=%d\nscale=%d\nmargin=%.4f\n",r->action,r->source,r->output,r->uri,r->layout.paper,r->layout.landscape,r->layout.scale,r->layout.margin_pt);
 {int bad=ferror(f);if(fclose(f))bad=1;if(bad){remove(path);copystr(err,errcap,"Request write failed");return 0;}}
 snprintf(cmd,sizeof(cmd),"C:OAVWorker %s",path);
 rc=SystemTags((STRPTR)cmd,SYS_Asynch,TRUE,NP_StackSize,65536,SYS_InName,(ULONG)"NIL:",SYS_OutName,(ULONG)"NIL:",TAG_DONE);
 if(rc<0){copystr(err,errcap,"Cannot start C:OAVWorker; request preserved");copystr(request,cap,path);return 0;}
 copystr(request,cap,path);copystr(err,errcap,"Job started; original-resolution source, background worker");return 1;
}
int oav_read_request(const char *path,OAVRequest *r)
{
 FILE *f=fopen(path,"r");char line[1024];int schema=0;if(!f)return 0;
 memset(r,0,sizeof(*r));oav_layout_defaults(&r->layout);
 while(fgets(line,sizeof(line),f)){
  char *v=strchr(line,'='),*e;if(!v)continue;*v++=0;e=strpbrk(v,"\r\n");if(e)*e=0;
  if(!strcmp(line,"schema"))schema=atoi(v);
  else if(!strcmp(line,"source"))copystr(r->source,sizeof(r->source),v);
  else if(!strcmp(line,"output"))copystr(r->output,sizeof(r->output),v);
  else if(!strcmp(line,"uri"))copystr(r->uri,sizeof(r->uri),v);
  else if(!strcmp(line,"action"))copystr(r->action,sizeof(r->action),v);
  else if(!strcmp(line,"paper"))r->layout.paper=atoi(v);
  else if(!strcmp(line,"landscape"))r->layout.landscape=atoi(v);
  else if(!strcmp(line,"scale"))r->layout.scale=atoi(v);
  else if(!strcmp(line,"margin"))r->layout.margin_pt=strtod(v,NULL);
 }
 fclose(f);return schema==1&&oav_safe_field(r->source);
}
int oav_result(const char *req,char *state,size_t statecap,char *msg,size_t msgcap)
{
 char p[OAV_PATH_MAX+16],line[1024];FILE *f;
 snprintf(p,sizeof(p),"%s.result",req);f=fopen(p,"r");if(!f)return 0;
 if(statecap)state[0]=0;if(msgcap)msg[0]=0;
 while(fgets(line,sizeof(line),f)){
  char *v=strchr(line,'='),*e;if(!v)continue;*v++=0;e=strpbrk(v,"\r\n");if(e)*e=0;
  if(!strcmp(line,"state"))copystr(state,statecap,v);
  if(!strcmp(line,"message"))copystr(msg,msgcap,v);
 }
 fclose(f);return state[0]!=0;
}
int oav_cancel(const char *req)
{
 char p[OAV_PATH_MAX+16];FILE *f;
 if(!req||strncmp(req,OAV_REQUEST_DIR "/v-",strlen(OAV_REQUEST_DIR "/v-")))return 0;
 snprintf(p,sizeof(p),"%s.cancel",req);f=fopen(p,"w");if(!f)return 0;fputs("cancel\n",f);return fclose(f)==0;
}
