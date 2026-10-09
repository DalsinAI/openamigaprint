/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* Deterministic transport tests: no sockets and no real printer jobs. */
#include "oap.h"
#include "oap_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks, mode, writes, opened, closed, cancel_mode, queried;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct OAPConn { int magic; };
static struct OAPConn the_conn = { 42 };
static int receives;
int oap_net_start(void){return 1;}
void oap_net_stop(void){}
OAPConn *oap_conn_open(const OAPUri *u,int mode,OAPTlsInfo *t,char *note,size_t n){
 (void)note;(void)n;CHECK(mode==OAP_TLS_VERIFY);if(t){memset(t,0,sizeof(*t));t->verdict=u->secure?OAP_CERT_TRUSTED:OAP_CERT_NONE;}opened++;return &the_conn;}
int oap_conn_trusted(const OAPConn *c){(void)c;return 1;}
void oap_conn_close(OAPConn *c){if(!c)return;CHECK(c->magic==42);closed++;}
int oap_conn_write(OAPConn *c,const void *buf,size_t n,unsigned s){(void)s;CHECK(c->magic==42);CHECK(buf!=NULL&&n>0);writes++;return mode!=4;}
static int authorized;
int oap_http_post(OAPConn *c,const OAPUri *u,unsigned long len,const char *auth){CHECK(u&&len>0);if(auth)authorized++;return oap_conn_write(c,"POST",4,5);}
int oap_ipp_login(OAPConn *c,const OAPUri *u,const OAPHttpReply *r,unsigned k,char *a,size_t cap,char *note,size_t nc){
 (void)c;(void)k;CHECK(r->status==401);if(!u->secure){snprintf(note,nc,"passwords only over ipps://");return 0;}snprintf(a,cap,"Authorization: Basic eA==\r\n");return 1;}
int oap_query_pdf(const char *uri,OAPCaps *c,OAPTlsInfo *t,char *note,size_t n){
 queried++;memset(c,0,sizeof(*c));c->pdf=mode==1?OAP_PDF_NO:OAP_PDF_YES;c->accepting=1;c->color=1;
 if(t){memset(t,0,sizeof(*t));t->verdict=!strncmp(uri,"ipps://",7)?(mode==7?OAP_CERT_ASK:mode==8?OAP_CERT_NOTLS:OAP_CERT_TRUSTED):OAP_CERT_NONE;}
 snprintf(note,n,"mock query");return mode!=8;
}
int oap_receive_ipp(OAPConn *c,unsigned char *out,size_t cap,size_t *n,unsigned secs,OAPHttpReply *r,char *note,size_t nc){
 const unsigned char p[]={1,1,0,0,0,0,0,1,2,0x21,0,6,'j','o','b','-','i','d',0,4,0,0,0,42,3};
 (void)secs;CHECK(c->magic==42);CHECK(cap>=sizeof(p));memset(r,0,sizeof(*r));receives++;
 if(mode==2){snprintf(note,nc,"mock lost reply");return 0;}
 if((mode==5||mode==6)&&receives==1){r->status=401;snprintf(r->challenge,sizeof(r->challenge),"Basic realm=\"x\"");snprintf(note,nc,"401");return 0;}
 r->status=200;memcpy(out,p,sizeof(p));*n=mode==3?8:sizeof(p);return 1;
}
static int progress(void *ctx,const char *stage,unsigned long sent,unsigned long total){
 (void)ctx;CHECK(sent<=total);
 if(cancel_mode==1&&!strcmp(stage,"preparing"))return 0;
 if(cancel_mode==2&&!strcmp(stage,"uploading")&&sent>=8192)return 0;
 return 1;
}
static void reset(int m,int cancel){mode=m;cancel_mode=cancel;writes=opened=closed=queried=receives=authorized=0;}
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
 /* IPPS: a 401 is a plain "not taken", so the job goes again once with the login; never over ipp:// */
 strcpy(o.printer_uri,"ipps://192.0.2.123/ipp/print");
 reset(5,0);rc=oap_ipp_submit_pdf_ex("build/submit-test-fixture.dat",&o,status,sizeof(status),progress,NULL);CHECK(rc==OAP_SEND_ACCEPTED&&authorized==1&&writes==10&&receives==2);CHECK(strstr(status,"over IPPS")!=NULL);
 strcpy(o.printer_uri,"ipp://192.0.2.123/ipp/print");
 reset(6,0);rc=oap_ipp_submit_pdf_ex("build/submit-test-fixture.dat",&o,status,sizeof(status),progress,NULL);CHECK(rc==OAP_SEND_ERROR&&authorized==0&&strstr(status,"ipps://")!=NULL);
 /* IPPS printers not trusted yet, or no TLS library: nothing is sent */
 strcpy(o.printer_uri,"ipps://192.0.2.123/ipp/print");
 reset(7,0);rc=oap_ipp_submit_pdf_ex("build/submit-test-fixture.dat",&o,status,sizeof(status),progress,NULL);CHECK(rc==OAP_SEND_ERROR&&writes==0&&opened==0&&strstr(status,"isn't trusted")!=NULL);
 reset(8,0);rc=oap_ipp_submit_pdf_ex("build/submit-test-fixture.dat",&o,status,sizeof(status),progress,NULL);CHECK(rc==OAP_SEND_ERROR&&writes==0&&strstr(status,"opentls.library")!=NULL);
 remove("build/submit-test-fixture.dat");printf("PASS: %d submission, cancellation, uncertainty and cleanup checks; no network used\n",checks);return 0;
}
