/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
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
 fprintf(f,"schema=3\naction=%s\nsource=%s\noutput=%s\nuri=%s\npaper=%d\nlandscape=%d\nscale=%d\nmargin_cpt=%ld\n",r->action,r->source,r->output,r->uri,r->layout.paper,r->layout.landscape,r->layout.scale,r->layout.margin_cpt);
 if(r->has_print_options)fprintf(f,"print_options=1\ncopies=%d\nprint_paper=%d\norientation=%d\ncolor=%d\nduplex=%d\npage_start=%d\npage_end=%d\n",r->print.copies,r->print.paper,r->print.orientation,r->print.color,r->print.duplex,r->print.page_start,r->print.page_end);
 {int bad=ferror(f);if(fclose(f))bad=1;if(bad){remove(path);copystr(err,errcap,"Request write failed");return 0;}}
 snprintf(cmd,sizeof(cmd),"Stack 65536\nC:OAVWorker %s",path);
 rc=SystemTags((STRPTR)cmd,SYS_Asynch,TRUE,NP_StackSize,65536,SYS_InName,(ULONG)"NIL:",SYS_OutName,(ULONG)"NIL:",TAG_DONE);
 if(rc<0){copystr(err,errcap,"Cannot start C:OAVWorker; request preserved");copystr(request,cap,path);return 0;}
 copystr(request,cap,path);copystr(err,errcap,"Job started; original-resolution source, background worker");return 1;
}
int oav_read_request(const char *path,OAVRequest *r)
{
 FILE *f=fopen(path,"r");char line[1024];int schema=0;if(!f)return 0;
 memset(r,0,sizeof(*r));oav_layout_defaults(&r->layout);r->print.copies=1;r->print.color=OAP_COLOR;
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
  else if(!strcmp(line,"margin_cpt"))r->layout.margin_cpt=strtol(v,NULL,10);
  else if(!strcmp(line,"print_options"))r->has_print_options=atoi(v)==1;
  else if(!strcmp(line,"copies"))r->print.copies=atoi(v);
  else if(!strcmp(line,"print_paper"))r->print.paper=atoi(v);
  else if(!strcmp(line,"orientation"))r->print.orientation=atoi(v);
  else if(!strcmp(line,"color"))r->print.color=atoi(v);
  else if(!strcmp(line,"duplex"))r->print.duplex=atoi(v);
  else if(!strcmp(line,"page_start"))r->print.page_start=atoi(v);
  else if(!strcmp(line,"page_end"))r->print.page_end=atoi(v);
 }
 fclose(f);if(r->has_print_options&&(r->print.copies<1||r->print.copies>999||r->print.paper<0||r->print.paper>1||r->print.orientation<0||r->print.orientation>1||r->print.color<0||r->print.color>1||r->print.duplex<0||r->print.duplex>2||r->print.page_start<0||r->print.page_end<r->print.page_start))return 0;
 return (schema==2||schema==3)&&oav_safe_field(r->source);
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

int oav_result_terminal(const char *s)
{
 return s&&(!strcmp(s,"ready")||!strcmp(s,"submitted")||!strcmp(s,"error")||!strcmp(s,"uncertain")||!strcmp(s,"cancelled"));
}
int oav_update_queue_state(const char *pdf,const char *state,const char *uri,const char *message)
{
 char path[OAV_PATH_MAX+16],tmp[OAV_PATH_MAX+24],line[1024];FILE *in,*out;size_t n;int bad=0;
 if(!pdf||strncmp(pdf,OAP_QUEUE_DIR "/",strlen(OAP_QUEUE_DIR "/")))return 1;
 n=strlen(pdf);if(n<4||n>=OAV_PATH_MAX||strcmp(pdf+n-4,".pdf"))return 0;
 strcpy(path,pdf);strcpy(path+n-4,".job");snprintf(tmp,sizeof(tmp),"%s.tmp",path);
 in=fopen(path,"r");out=fopen(tmp,"w");if(!out){if(in)fclose(in);return 0;}
 if(in){while(fgets(line,sizeof(line),in)){if(strncmp(line,"state=",6)&&strncmp(line,"printer=",8)&&strncmp(line,"message=",8))fputs(line,out);}if(ferror(in))bad=1;fclose(in);}
 else fprintf(out,"pdf=%s\n",pdf);
 fprintf(out,"state=%s\nprinter=%s\nmessage=%s\n",state,uri?uri:"",message?message:"");
 if(ferror(out))bad=1;if(fclose(out))bad=1;if(bad){remove(tmp);return 0;}
 remove(path);if(rename(tmp,path)){remove(tmp);return 0;}return 1;
}
