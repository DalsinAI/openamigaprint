/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* Deterministic transport tests: no sockets and no real printer jobs. */
#include "oap.h"
#include "oap_discovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks, mode, writes, opened, closed, cancel_mode, queried;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int oap_net_start(void){return 1;}
void oap_net_stop(void){}
int oap_net_connect(const char *h,unsigned p){(void)h;(void)p;opened++;return 42;}
void oap_net_close(int fd){CHECK(fd==42);closed++;}
int oap_net_write(int fd,const void *buf,size_t n){CHECK(fd==42);CHECK(buf!=NULL&&n>0);writes++;return mode!=4;}
int oap_query_pdf(const char *uri,OAPCaps *c,char *note,size_t n){
 (void)uri;queried++;memset(c,0,sizeof(*c));c->pdf=mode==1?OAP_PDF_NO:OAP_PDF_YES;c->accepting=1;c->color=1;
 snprintf(note,n,"mock query");return 1;
}
int oap_receive_ipp(int fd,unsigned char *out,size_t cap,size_t *n,char *note,size_t nc){
 const unsigned char p[]={1,1,0,0,0,0,0,1,2,0x21,0,6,'j','o','b','-','i','d',0,4,0,0,0,42,3};
 CHECK(fd==42);CHECK(cap>=sizeof(p));if(mode==2){snprintf(note,nc,"mock lost reply");return 0;}
 memcpy(out,p,sizeof(p));*n=mode==3?8:sizeof(p);return 1;
}
static int progress(void *ctx,const char *stage,unsigned long sent,unsigned long total){
 (void)ctx;CHECK(sent<=total);
 if(cancel_mode==1&&!strcmp(stage,"preparing"))return 0;
 if(cancel_mode==2&&!strcmp(stage,"uploading")&&sent>=8192)return 0;
 return 1;
}
static void reset(int m,int cancel){mode=m;cancel_mode=cancel;writes=opened=closed=queried=0;}
int main(void){
 OAPJobOptions o;char status[256];FILE *f;int rc,i;
 oap_job_defaults(&o);strcpy(o.printer_uri,"ipp://192.0.2.123:631/ipp/print");
 f=fopen("build/submit-test-fixture.dat","wb");CHECK(f!=NULL);fputs("%PDF-1.4\n",f);for(i=0;i<20000;i++)fputc('x',f);fclose(f);
 reset(0,0);rc=oap_ipp_submit_pdf_ex("build/submit-test-fixture.dat",&o,status,sizeof(status),progress,NULL);CHECK(rc==OAP_SEND_ACCEPTED);CHECK(strstr(status,"Accepted job 42")!=NULL);CHECK(writes==5&&opened==1&&closed==1);
 reset(1,0);rc=oap_ipp_submit_pdf_ex("build/submit-test-fixture.dat",&o,status,sizeof(status),progress,NULL);CHECK(rc==OAP_SEND_ERROR&&writes==0);
 reset(0,1);rc=oap_ipp_submit_pdf_ex("build/submit-test-fixture.dat",&o,status,sizeof(status),progress,NULL);CHECK(rc==OAP_SEND_CANCELLED&&writes==0&&queried==0);
 reset(0,2);rc=oap_ipp_submit_pdf_ex("build/submit-test-fixture.dat",&o,status,sizeof(status),progress,NULL);CHECK(rc==OAP_SEND_UNCERTAIN&&writes==3&&closed==1);
 for(i=2;i<=4;i++){reset(i,0);rc=oap_ipp_submit_pdf_ex("build/submit-test-fixture.dat",&o,status,sizeof(status),progress,NULL);CHECK(rc==OAP_SEND_UNCERTAIN&&closed==1);}
 reset(0,0);o.duplex=OAP_DUPLEX_LONG;rc=oap_ipp_submit_pdf_ex("build/submit-test-fixture.dat",&o,status,sizeof(status),progress,NULL);CHECK(rc==OAP_SEND_ERROR&&writes==0);o.duplex=OAP_SIMPLEX;
 reset(0,0);rc=oap_ipp_submit_pdf_ex("build/no-such-file",&o,status,sizeof(status),progress,NULL);CHECK(rc==OAP_SEND_ERROR&&writes==0);
 strcpy(o.printer_uri,"invalid");reset(0,0);rc=oap_ipp_submit_pdf_ex("build/submit-test-fixture.dat",&o,status,sizeof(status),progress,NULL);CHECK(rc==OAP_SEND_ERROR&&writes==0);
 remove("build/submit-test-fixture.dat");printf("PASS: %d submission, cancellation, uncertainty and cleanup checks; no network used\n",checks);return 0;
}
