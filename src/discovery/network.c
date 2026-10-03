/* SPDX-License-Identifier: BSD-2-Clause */
#if !defined(__amigaos__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif
#include "oap_discovery.h"
#ifdef __amigaos__
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#ifdef __amigaos__
struct Library *SocketBase;
#define close_sock(s) CloseSocket(s)
#define nonblock(s,p) IoctlSocket(s,FIONBIO,p)
#define select_sock(n,r,w,e,t) WaitSelect(n,r,w,e,t,NULL)
#else
#define close_sock(s) close(s)
#define nonblock(s,p) ioctl(s,FIONBIO,p)
#define select_sock(n,r,w,e,t) select(n,r,w,e,t)
#endif
int oap_net_start(void){
#ifdef __amigaos__
 if(!SocketBase)SocketBase=OpenLibrary((STRPTR)"bsdsocket.library",4);return SocketBase!=NULL;
#else
 return 1;
#endif
}
void oap_net_stop(void){
#ifdef __amigaos__
 if(SocketBase){CloseLibrary(SocketBase);SocketBase=NULL;}
#endif
}
static int ready(int fd,int write_mode,unsigned millis){fd_set r,w;struct timeval tv;FD_ZERO(&r);FD_ZERO(&w);if(fd<0||fd>=FD_SETSIZE)return 0;if(write_mode)FD_SET(fd,&w);else FD_SET(fd,&r);tv.tv_sec=millis/1000;tv.tv_usec=(millis%1000)*1000;return select_sock(fd+1,write_mode?NULL:&r,write_mode?&w:NULL,NULL,&tv)>0;}
static int split_uri(const char *uri,char *host,size_t hc,unsigned *port,char *path,size_t pc){const char *s,*end,*slash,*colon;size_t n,i;*port=631;if(strncmp(uri,"ipp://",6))return 0;s=uri+6;slash=strchr(s,'/');end=slash?slash:s+strlen(s);colon=memchr(s,':',(size_t)(end-s));n=(size_t)((colon?colon:end)-s);if(!n||n>=hc)return 0;for(i=0;i<n;i++)if((unsigned char)s[i]<=32||s[i]=='@'||s[i]=='['||s[i]==']'||s[i]=='\\')return 0;memcpy(host,s,n);host[n]=0;if(colon){unsigned v=0;const char *q=colon+1;if(q==end)return 0;for(;q<end;q++){if(*q<'0'||*q>'9'||v>6553)return 0;v=v*10+(unsigned)(*q-'0');}if(!v||v>65535)return 0;*port=v;}s=slash?slash:"/";n=strlen(s);if(n>=pc)return 0;for(i=0;i<n;i++)if((unsigned char)s[i]<=32||(unsigned char)s[i]>=127||s[i]=='#')return 0;memcpy(path,s,n+1);return 1;}
int oap_net_connect(const char *host,unsigned port){int fd,nb=1,err=0;struct sockaddr_in sa;socklen_t elen=sizeof(err);struct hostent *he;memset(&sa,0,sizeof(sa));sa.sin_family=AF_INET;sa.sin_port=htons((unsigned short)port);sa.sin_addr.s_addr=inet_addr((char *)host);if(sa.sin_addr.s_addr==INADDR_NONE){he=gethostbyname((char *)host);if(!he||he->h_addrtype!=AF_INET||he->h_length!=4||!he->h_addr_list[0])return -1;memcpy(&sa.sin_addr,he->h_addr_list[0],4);}fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return -1;if(nonblock(fd,&nb)<0){close_sock(fd);return -1;}if(connect(fd,(struct sockaddr *)&sa,sizeof(sa))<0){if(!ready(fd,1,3500)||getsockopt(fd,SOL_SOCKET,SO_ERROR,&err,&elen)<0||err){close_sock(fd);return -1;}}return fd;}
int oap_net_write(int fd,const void *v,size_t n){const unsigned char *p=v;time_t end=time(NULL)+5;while(n){long z;if(time(NULL)>end||!ready(fd,1,1000))return 0;z=send(fd,(char *)p,n,0);if(z<=0)return 0;p+=z;n-=(size_t)z;}return 1;}
int oap_receive_ipp(int fd,unsigned char *body,size_t cap,size_t *bodylen,char *note,size_t nc){unsigned char *raw=malloc(OAP_HTTP_MAX);size_t n=0;int http=0,ok=0,eof=0;time_t end=time(NULL)+8;if(!raw){snprintf(note,nc,"Not enough memory for bounded IPP response");return 0;}while(time(NULL)<=end){int r;long got;if(!ready(fd,0,1000))continue;got=recv(fd,(char *)raw+n,OAP_HTTP_MAX-n,0);if(got<0){snprintf(note,nc,"IPP response interrupted");break;}if(!got)eof=1;else n+=(size_t)got;r=oap_http_response(raw,n,eof,body,cap,bodylen,&http);if(r==1){ok=1;break;}if(r<0){snprintf(note,nc,"Invalid/incomplete IPP HTTP response (HTTP %d)",http);break;}if(eof||n==OAP_HTTP_MAX){snprintf(note,nc,"Truncated or oversized IPP response");break;}}if(!ok&&!note[0])snprintf(note,nc,"IPP response timed out");free(raw);return ok;}
int oap_query_pdf(const char *uri,OAPCaps *caps,char *note,size_t nc){char host[256],path[512],hdr[1024];unsigned port;unsigned char req[2048],*body=NULL;size_t rn,bn;long hn;int fd=-1,ok=0;uint32_t id=0x4f415001U;memset(caps,0,sizeof(*caps));caps->accepting=-1;note[0]=0;if(!strncmp(uri,"ipps://",7)){snprintf(note,nc,"Secure endpoint discovered; TLS query not available in this browser build");return 0;}if(!split_uri(uri,host,sizeof(host),&port,path,sizeof(path))){snprintf(note,nc,"Invalid IPv4 ipp:// endpoint");return 0;}rn=oap_ipp_query_request(uri,id,req,sizeof(req));if(!rn)return 0;body=malloc(OAP_BODY_MAX);if(!body){snprintf(note,nc,"Not enough memory");return 0;}fd=oap_net_connect(host,port);if(fd<0){snprintf(note,nc,"Printer connection failed or timed out");goto out;}hn=snprintf(hdr,sizeof(hdr),"POST %s HTTP/1.1\r\nHost: %s:%u\r\nContent-Type: application/ipp\r\nContent-Length: %lu\r\nConnection: close\r\nUser-Agent: OpenAmigaPrint/0.2\r\n\r\n",path,host,port,(unsigned long)rn);if(hn<0||hn>=(long)sizeof(hdr)||!oap_net_write(fd,hdr,(size_t)hn)||!oap_net_write(fd,req,rn)){snprintf(note,nc,"Capability query send failed");goto out;}if(!oap_receive_ipp(fd,body,OAP_BODY_MAX,&bn,note,nc))goto out;if(!oap_ipp_parse_caps(body,bn,id,caps)){snprintf(note,nc,"IPP query rejected or malformed; PDF not verified");goto out;}ok=1;if(caps->pdf==OAP_PDF_YES)snprintf(note,nc,"PDF confirmed; %s%s%s",caps->accepting==0?"not accepting jobs":caps->state==5?"stopped":caps->state==4?"processing":"ready",caps->reasons[0]?"; ":"",caps->reasons);else if(caps->pdf==OAP_PDF_NO)snprintf(note,nc,"Printer does not advertise application/pdf");else snprintf(note,nc,"Printer omitted document-format-supported; not verified");
out:if(fd>=0)close_sock(fd);free(body);return ok;}
static int dns_send(int fd,const OAPName *name,unsigned type)
{
    unsigned char p[300];struct sockaddr_in sa;size_t n;
    n=oap_dns_query(name,type,0x4f41,p,sizeof(p));
    memset(&sa,0,sizeof(sa));sa.sin_family=AF_INET;sa.sin_port=htons(5353);
    sa.sin_addr.s_addr=inet_addr((char *)"224.0.0.251");
    return n && sendto(fd,(char *)p,n,0,(struct sockaddr *)&sa,sizeof(sa))==(long)n;
}
static int network_error(void)
{
#ifdef __amigaos__
    return (int)Errno();
#else
    return errno;
#endif
}
int oap_discover_run(const char *output)
{
    OAPDiscovery *d=calloc(1,sizeof(*d));OAPName name;
    int fd=-1,nb=1;unsigned phase,received=0,parsed=0,sent=0;
    size_t i;struct sockaddr_in sa;unsigned char packet[9000];
    char failure[256]="Cannot open local-network discovery socket";
    if(!d)return 20;
    oap_discovery_save(output,d,0,"Opening bsdsocket.library...");
    if(!oap_net_start()){
        oap_discovery_save(output,d,1,"bsdsocket.library is not available");
        free(d);return 20;
    }
    oap_discovery_save(output,d,0,"Opening discovery UDP socket...");
    fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)goto fail;
    memset(&sa,0,sizeof(sa));sa.sin_family=AF_INET;
    sa.sin_port=0;sa.sin_addr.s_addr=INADDR_ANY;
    if(bind(fd,(struct sockaddr *)&sa,sizeof(sa))<0||nonblock(fd,&nb)<0)goto fail;
    oap_discovery_save(output,d,0,"Discovering IPP printers on the local network...");
    /* One-shot mDNS, ephemeral source port: RFC 6762 section 6.7. */
#define SEND_DNS(n,t) do { \
    if(!dns_send(fd,(n),(t))){ \
        snprintf(failure,sizeof(failure), \
          "Discovery send failed (error %d): check UDP mDNS access to 224.0.0.251:5353", \
          network_error());goto fail; \
    } sent++; \
} while(0)
    for(phase=0;phase<3;phase++){
        time_t end=time(NULL)+2;unsigned packets=0;
        oap_dns_name_text("_ipp._tcp.local",&name);SEND_DNS(&name,12);
        oap_dns_name_text("_ipps._tcp.local",&name);SEND_DNS(&name,12);
        for(i=0;i<d->count;i++){
            OAPDiscovered *e=&d->printers[i];
            if(!e->have_srv)SEND_DNS(&e->service,33);
            if(!e->have_txt)SEND_DNS(&e->service,16);
            if(e->target.len&&!e->ip[0])SEND_DNS(&e->target,1);
        }
        while(time(NULL)<end&&packets<512){
            long got;socklen_t sl=sizeof(sa);
            if(!ready(fd,0,180))continue;
            got=recvfrom(fd,(char *)packet,sizeof(packet),0,(struct sockaddr *)&sa,&sl);
            if(got<=0)continue;
            packets++;received++;
            if(sa.sin_port!=htons(5353)||got<12)continue;
            if((packet[0]||packet[1])&&(packet[0]!=0x4f||packet[1]!=0x41))continue;
            if(oap_dns_packet(d,packet,(size_t)got)){
                parsed++;
                oap_discovery_save(output,d,0,"Resolving printer names, ports and resource paths...");
            }
        }
    }
#undef SEND_DNS
    close_sock(fd);fd=-1;
    for(i=0;i<d->count;i++){
        OAPDiscovered *e=&d->printers[i];char msg[180];
        if(!e->alive)continue;
        if(!e->uri[0]){strcpy(e->note,"Incomplete DNS-SD endpoint; cannot verify");continue;}
        snprintf(msg,sizeof(msg),"Checking PDF support: %.120s",e->label);
        strcpy(e->note,"Querying Get-Printer-Attributes...");
        oap_discovery_save(output,d,0,msg);
        oap_query_pdf(e->uri,&e->caps,e->note,sizeof(e->note));
        oap_discovery_save(output,d,0,msg);
    }
    {
        unsigned yes=0;char msg[256];
        for(i=0;i<d->count;i++)if(oap_pdf_eligible(&d->printers[i]))yes++;
        if(!received)snprintf(msg,sizeof(msg),
          "No discovery replies (%u queries sent). Check printer power and LAN multicast access.",sent);
        else snprintf(msg,sizeof(msg),
          "Discovery complete: %lu endpoints, %u verified PDF; %u replies, %u parsed",
          (unsigned long)d->count,yes,received,parsed);
        oap_discovery_save(output,d,1,msg);
    }
    oap_net_stop();free(d);return 0;
fail:
    if(fd>=0)close_sock(fd);
    oap_discovery_save(output,d,1,failure);oap_net_stop();free(d);return 20;
}
