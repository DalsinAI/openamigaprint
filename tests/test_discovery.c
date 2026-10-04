/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#include "oap_discovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void p16(unsigned char *p,unsigned x){p[0]=x>>8;p[1]=x;}
static size_t attr(unsigned char *p,size_t x,unsigned t,const char *key,const char *v){size_t a=strlen(key),b=strlen(v);p[x++]=t;p16(p+x,a);x+=2;memcpy(p+x,key,a);x+=a;p16(p+x,b);x+=2;memcpy(p+x,v,b);return x+b;}
static size_t response(unsigned char *p,const char *mime){size_t x=8;memset(p,0,1024);p[0]=1;p[1]=1;p[7]=42;p[x++]=4;x=attr(p,x,0x41,"printer-make-and-model","HP test");x=attr(p,x,0x49,"document-format-supported","image/pwg-raster");if(mime)x=attr(p,x,0x49,"",mime);p[x++]=3;return x;}
static size_t rr(unsigned char *p,size_t x,const OAPName *owner,unsigned t,const void *data,size_t n){memcpy(p+x,owner->wire,owner->len);x+=owner->len;p16(p+x,t);p16(p+x+2,0x8001);p[x+7]=120;p16(p+x+8,n);x+=10;memcpy(p+x,data,n);return x+n;}
int main(void){unsigned char ipp[1024],http[4096],out[2048],pkt[2048],rd[512];OAPCaps caps;size_t n,hn,bn,i,x;int status;OAPName service,host,base;OAPDiscovery *d=calloc(1,sizeof(*d));
 n=response(ipp,"application/pdf");CHECK(oap_ipp_parse_caps(ipp,n,42,&caps));CHECK(caps.pdf==OAP_PDF_YES);CHECK(!strcmp(caps.model,"HP test"));for(i=0;i<n;i++)CHECK(!oap_ipp_parse_caps(ipp,i,42,&caps));CHECK(!oap_ipp_parse_caps(ipp,n,43,&caps));
 n=response(ipp,"application/pdf-invalid");CHECK(oap_ipp_parse_caps(ipp,n,42,&caps));CHECK(caps.pdf==OAP_PDF_NO);
 n=response(ipp,NULL);CHECK(oap_ipp_parse_caps(ipp,n,42,&caps));CHECK(caps.pdf==OAP_PDF_NO);
 memset(ipp,0,9);ipp[0]=1;ipp[7]=42;ipp[8]=3;CHECK(oap_ipp_parse_caps(ipp,9,42,&caps));CHECK(caps.pdf==OAP_PDF_UNKNOWN);
 n=response(ipp,"application/pdf");ipp[2]=4;CHECK(!oap_ipp_parse_caps(ipp,n,42,&caps));ipp[2]=0;
 hn=(size_t)sprintf((char *)http,"HTTP/1.1 200 OK\r\nContent-Type: application/ipp\r\nContent-Length: %lu\r\n\r\n",(unsigned long)n);memcpy(http+hn,ipp,n);hn+=n;for(i=0;i<hn;i++)CHECK(oap_http_response(http,i,0,out,sizeof(out),&bn,&status)==0);CHECK(oap_http_response(http,hn,0,out,sizeof(out),&bn,&status)==1);CHECK(bn==n&&!memcmp(ipp,out,n));CHECK(oap_http_response(http,hn-1,1,out,sizeof(out),&bn,&status)==-1);
 hn=(size_t)sprintf((char *)http,"HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Type: application/ipp\r\nTransfer-Encoding: chunked\r\n\r\n%lx\r\n",(unsigned long)n);memcpy(http+hn,ipp,n);hn+=n;memcpy(http+hn,"\r\n0\r\n\r\n",7);hn+=7;for(i=0;i<hn;i++)CHECK(oap_http_response(http,i,0,out,sizeof(out),&bn,&status)==0);CHECK(oap_http_response(http,hn,0,out,sizeof(out),&bn,&status)==1);CHECK(bn==n&&!memcmp(ipp,out,n));
 strcpy((char *)http,"HTTP/1.1 200 OK\r\nContent-Type: application/ipp\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nAB");CHECK(oap_http_response(http,strlen((char *)http),1,out,sizeof(out),&bn,&status)==-1);
 oap_dns_name_text("HP LaserJet._ipp._tcp.local",&service);oap_dns_name_text("hp.local",&host);oap_dns_name_text("_ipp._tcp.local",&base);memset(pkt,0,sizeof(pkt));pkt[2]=0x84;p16(pkt+6,4);x=12;
 {unsigned char ip[4]={192,168,0,6};x=rr(pkt,x,&host,1,ip,4);}rd[0]=12;memcpy(rd+1,"rp=ipp/print",12);x=rr(pkt,x,&service,16,rd,13);x=rr(pkt,x,&base,12,service.wire,service.len);memset(rd,0,6);p16(rd+4,8631);memcpy(rd+6,host.wire,host.len);x=rr(pkt,x,&service,33,rd,6+host.len);
 CHECK(oap_dns_packet(d,pkt,x));CHECK(d->count==1);CHECK(!strcmp(d->printers[0].uri,"ipp://192.168.0.6:8631/ipp/print"));CHECK(!oap_pdf_eligible(&d->printers[0]));d->printers[0].caps.pdf=OAP_PDF_YES;CHECK(oap_pdf_eligible(&d->printers[0]));d->printers[0].secure=1;CHECK(!oap_pdf_eligible(&d->printers[0]));
 for(i=0;i<x;i++){OAPDiscovery *bad=calloc(1,sizeof(*bad));CHECK(!oap_dns_packet(bad,pkt,i));CHECK(bad->count==0);free(bad);}
 memset(pkt,0,20);pkt[2]=0x84;p16(pkt+6,1);pkt[12]=0xc0;pkt[13]=12;CHECK(!oap_dns_packet(d,pkt,20));
 n=oap_ipp_query_request("ipp://192.168.0.6:631/ipp/print",42,ipp,sizeof(ipp));CHECK(n>0&&ipp[3]==11&&ipp[n-1]==3);CHECK(!oap_ipp_query_request("ipp://host/print",42,ipp,8));
 /* Print-Job acknowledgements must carry a valid integer job-id. */
 memset(ipp,0,32);ipp[0]=1;ipp[1]=1;ipp[7]=42;ipp[8]=2;ipp[9]=0x21;p16(ipp+10,6);memcpy(ipp+12,"job-id",6);p16(ipp+18,4);ipp[23]=7;ipp[24]=3;
 CHECK(oap_ipp_parse_caps(ipp,25,42,&caps));CHECK(caps.job_id==7);CHECK(!oap_ipp_parse_caps(ipp,24,42,&caps));
 free(d);printf("PASS: %u discovery, MIME gating, DNS bounds, complete HTTP and IPP checks\n",checks);return 0;}
