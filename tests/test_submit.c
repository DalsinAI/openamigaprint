/* Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT */
/* Deterministic transport tests: no sockets and no real printer jobs. */
#define _GNU_SOURCE                    /* memmem */
#include "oap.h"
#include "oap_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks, mode, writes, opened, closed, cancel_mode, queried, ps_caps = OAP_PDF_YES;
static unsigned char sent[65536];      /* everything written to the printer, the request line's stand-in too */
static size_t sent_n;
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
int oap_conn_write(OAPConn *c,const void *buf,size_t n,unsigned s){(void)s;CHECK(c->magic==42);CHECK(buf!=NULL&&n>0);writes++;
 if(sent_n+n<=sizeof(sent)){memcpy(sent+sent_n,buf,n);sent_n+=n;}return mode!=4;}
static int authorized;
int oap_http_post(OAPConn *c,const OAPUri *u,unsigned long len,const char *auth){CHECK(u&&len>0);if(auth)authorized++;return oap_conn_write(c,"POST",4,5);}
int oap_ipp_login(OAPConn *c,const OAPUri *u,const OAPHttpReply *r,unsigned k,char *a,size_t cap,char *note,size_t nc){
 (void)c;(void)k;CHECK(r->status==401);if(!u->secure){snprintf(note,nc,"passwords only over ipps://");return 0;}snprintf(a,cap,"Authorization: Basic eA==\r\n");return 1;}
int oap_query_pdf(const char *uri,OAPCaps *c,OAPTlsInfo *t,char *note,size_t n){
 queried++;memset(c,0,sizeof(*c));c->pdf=mode==1?OAP_PDF_NO:OAP_PDF_YES;c->ps=ps_caps;c->accepting=1;c->color=1;
 if(t){memset(t,0,sizeof(*t));t->verdict=!strncmp(uri,"ipps://",7)?(mode==7?OAP_CERT_ASK:mode==8?OAP_CERT_NOTLS:OAP_CERT_TRUSTED):OAP_CERT_NONE;}
 snprintf(note,n,"mock query");return mode!=8;
}
int oap_receive_ipp(OAPConn *c,unsigned char *out,size_t cap,size_t *n,unsigned secs,OAPHttpReply *r,char *note,size_t nc){
 const unsigned char p[]={1,1,0,0,0,0,0,1,2,0x21,0,6,'j','o','b','-','i','d',0,4,0,0,0,42,3};
 const unsigned char refused[]={1,1,0x04,0x0a,0,0,0,1,1,3};      /* client-error-document-format-not-supported */
 (void)secs;CHECK(c->magic==42);CHECK(cap>=sizeof(p));memset(r,0,sizeof(*r));receives++;
 if(mode==2){snprintf(note,nc,"mock lost reply");return 0;}
 if((mode==5||mode==6)&&receives==1){r->status=401;snprintf(r->challenge,sizeof(r->challenge),"Basic realm=\"x\"");snprintf(note,nc,"401");return 0;}
 r->status=200;if(mode==9){memcpy(out,refused,sizeof(refused));*n=sizeof(refused);return 1;}
 memcpy(out,p,sizeof(p));*n=mode==3?8:sizeof(p);return 1;
}
static int progress(void *ctx,const char *stage,unsigned long sent,unsigned long total){
 (void)ctx;CHECK(sent<=total);
 if(cancel_mode==1&&!strcmp(stage,"preparing"))return 0;
 if(cancel_mode==2&&!strcmp(stage,"uploading")&&sent>=8192)return 0;
 return 1;
}
static void reset(int m,int cancel){mode=m;cancel_mode=cancel;writes=opened=closed=queried=receives=authorized=0;sent_n=0;}
/* What a program's own PostScript sends (27 October 2026): asked first, then as it is. */
static void raw_postscript(void){
 static unsigned char doc[30000],want[32768],pdfsent[32768];OAPJobOptions o,e;char status[256];FILE *f;size_t i,n=0,pn,pdfn;int rc;
 n+=(size_t)sprintf((char *)doc,"%%!PS-Adobe-3.0\n%%%%Title: (Quarterly report)\n%%%%EndComments\n");
 for(i=0;i<20000;i++)doc[n++]=(unsigned char)(i*7+i/256);      /* every byte value, CR, LF, ^D and NUL among them */
 n+=(size_t)sprintf((char *)doc+n,"showpage\n%%%%EOF\n");
 f=fopen("build/submit-test.ps","wb");CHECK(f&&fwrite(doc,1,n,f)==n);fclose(f);
 oap_job_defaults(&o);strcpy(o.printer_uri,"ipp://192.0.2.123:631/ipp/print");o.copies=2;strcpy(o.job_name,"Raw PostScript");
 o.duplex=OAP_DUPLEX_LONG;      /* not the PostScript's business: no duplex check, no sides sent */
 e=o;strcpy(e.job_name,"Quarterly report");CHECK(oap_ipp_build_prefix_doc(&e,OAP_FORMAT_PS,want,sizeof(want),&pn));
 memcpy(want+pn,doc,n);
 ps_caps=OAP_PDF_YES;reset(0,0);rc=oap_ipp_submit_doc_ex("build/submit-test.ps",&o,status,sizeof(status),progress,NULL);
 CHECK(rc==OAP_SEND_ACCEPTED&&strstr(status,"Accepted job 42")!=NULL&&queried==1&&closed==1);
 CHECK(sent_n==4+pn+n&&!memcmp(sent,"POST",4)&&!memcmp(sent+4,want,pn+n));   /* its %%Title, copies 2, then every byte */
 ps_caps=OAP_PDF_NO;reset(0,0);rc=oap_ipp_submit_doc_ex("build/submit-test.ps",&o,status,sizeof(status),progress,NULL);
 CHECK(rc==OAP_SEND_ERROR&&writes==0&&opened==0&&queried==1&&strstr(status,"doesn't take PostScript")!=NULL);
 ps_caps=OAP_PDF_UNKNOWN;reset(0,0);rc=oap_ipp_submit_doc_ex("build/submit-test.ps",&o,status,sizeof(status),progress,NULL);
 CHECK(rc==OAP_SEND_ERROR&&writes==0&&strstr(status,"isn't confirmed")!=NULL);
 ps_caps=OAP_PDF_YES;reset(9,0);rc=oap_ipp_submit_doc_ex("build/submit-test.ps",&o,status,sizeof(status),progress,NULL);
 CHECK(rc==OAP_SEND_ERROR&&strstr(status,"doesn't take PostScript (IPP status 0x040a)")!=NULL);   /* a plain no, not "uncertain" */
 reset(0,0);rc=oap_ipp_submit_pdf_ex("build/submit-test.ps",&o,status,sizeof(status),progress,NULL);
 CHECK(rc==OAP_SEND_ERROR&&writes==0&&queried==0&&strstr(status,"not a PDF")!=NULL);           /* the PDF path stays PDF only */
 f=fopen("build/submit-test.pcl","wb");CHECK(f!=NULL);fputs("\033E\033&l0O PCL\f\033E",f);fclose(f);
 reset(0,0);rc=oap_ipp_submit_doc_ex("build/submit-test.pcl",&o,status,sizeof(status),progress,NULL);
 CHECK(rc==OAP_SEND_ERROR&&writes==0&&queried==0&&strstr(status,"neither")!=NULL);              /* PCL is kept, never sent */
 f=fopen("build/submit-test-untitled.ps","wb");CHECK(f!=NULL);fputs("\004%!PS-Adobe-2.0\nshowpage\n",f);fclose(f);
 o.job_name[0]=0;reset(0,0);rc=oap_ipp_submit_doc_ex("build/submit-test-untitled.ps",&o,status,sizeof(status),progress,NULL);
 CHECK(rc==OAP_SEND_ACCEPTED&&memmem(sent,sent_n,"submit-test-untitled.ps",23)!=NULL);           /* no title, no name: the file's */
 /* a PDF goes the same way by either door */
 f=fopen("build/submit-test.pdf","wb");CHECK(f!=NULL);fputs("%PDF-1.4\n%%EOF\n",f);fclose(f);
 o.duplex=OAP_SIMPLEX;strcpy(o.job_name,"A PDF");
 reset(0,0);rc=oap_ipp_submit_pdf_ex("build/submit-test.pdf",&o,status,sizeof(status),progress,NULL);CHECK(rc==OAP_SEND_ACCEPTED);
 pdfn=sent_n;memcpy(pdfsent,sent,sent_n);
 reset(0,0);rc=oap_ipp_submit_doc_ex("build/submit-test.pdf",&o,status,sizeof(status),progress,NULL);
 CHECK(rc==OAP_SEND_ACCEPTED&&sent_n==pdfn&&!memcmp(sent,pdfsent,pdfn)&&memmem(sent,sent_n,"application/pdf",15)!=NULL);
 remove("build/submit-test.ps");remove("build/submit-test.pcl");remove("build/submit-test-untitled.ps");remove("build/submit-test.pdf");
}
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
 raw_postscript();
 remove("build/submit-test-fixture.dat");printf("PASS: %d submission, cancellation, uncertainty and cleanup checks; no network used\n",checks);return 0;
}
