#include "oap.h"
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct Library *SocketBase;

static int send_all(int s,const void *data,long len)
{
    const unsigned char *p=(const unsigned char *)data;
    while(len>0){ long n=send(s,(char *)p,len,0); if(n<=0)return 0; p+=n; len-=n; }
    return 1;
}

static long file_size(FILE *f)
{
    long p,n; p=ftell(f); if(p<0)return -1; if(fseek(f,0,SEEK_END))return -1;
    n=ftell(f); if(n>=0)fseek(f,p,SEEK_SET); return n;
}
int oap_ipp_submit_pdf(const char *pdf,const OAPJobOptions *o,char *status,size_t status_len)
{
    OAPUri u; struct hostent *he; struct sockaddr_in sa; FILE *f=NULL;
    unsigned char prefix[2048],chunk[8192],resp[4096]; size_t plen=0;
    char hdr[1024]; long flen,n; int s=-1,http=0,ok=0; long rn=0;
    if(status&&status_len)snprintf(status,status_len,"Preparing job...");
    if(!oap_parse_ipp_uri(o->printer_uri,&u)){if(status)snprintf(status,status_len,"Invalid ipp:// printer URI");return 0;}
    f=fopen(pdf,"rb"); if(!f){if(status)snprintf(status,status_len,"Cannot open PDF spool file");return 0;}
    flen=file_size(f); if(flen<0||!oap_ipp_build_prefix(o,prefix,sizeof(prefix),&plen)){if(status)snprintf(status,status_len,"Cannot prepare IPP job");goto done;}
    SocketBase=OpenLibrary((STRPTR)"bsdsocket.library",4); if(!SocketBase){if(status)snprintf(status,status_len,"bsdsocket.library v4 not available");goto done;}
    he=gethostbyname((STRPTR)u.host); if(!he||!he->h_addr_list||!he->h_addr_list[0]){if(status)snprintf(status,status_len,"Cannot resolve printer host");goto done;}
    memset(&sa,0,sizeof(sa)); sa.sin_family=AF_INET; sa.sin_port=htons(u.port); memcpy(&sa.sin_addr,he->h_addr_list[0],sizeof(sa.sin_addr));
    s=socket(AF_INET,SOCK_STREAM,0); if(s<0){if(status)snprintf(status,status_len,"Cannot create TCP socket");goto done;}
    if(connect(s,(struct sockaddr *)&sa,sizeof(sa))<0){if(status)snprintf(status,status_len,"Cannot connect to printer");goto done;}
    n=snprintf(hdr,sizeof(hdr),"POST %s HTTP/1.1\r\nHost: %s:%u\r\nContent-Type: application/ipp\r\nContent-Length: %lu\r\nConnection: close\r\nUser-Agent: OpenAmigaPrint/0.1\r\n\r\n",u.path,u.host,(unsigned)u.port,(unsigned long)(plen+flen));
    if(n<=0||n>=(long)sizeof(hdr)||!send_all(s,hdr,n)||!send_all(s,prefix,(long)plen)){if(status)snprintf(status,status_len,"IPP request write failed");goto done;}
    while((n=(long)fread(chunk,1,sizeof(chunk),f))>0)if(!send_all(s,chunk,n)){if(status)snprintf(status,status_len,"PDF upload interrupted");goto done;}
    rn=recv(s,(char *)resp,sizeof(resp)-1,0); if(rn<=0){if(status)snprintf(status,status_len,"Printer returned no response");goto done;} resp[rn]=0;
    if(sscanf((char *)resp,"HTTP/%*s %d",&http)!=1||http<200||http>=300){if(status)snprintf(status,status_len,"Printer HTTP error %d",http);goto done;}
    { unsigned char *body=(unsigned char *)strstr((char *)resp,"\r\n\r\n"); if(body&&body+8<resp+rn){unsigned ipp;body+=4;ipp=((unsigned)body[2]<<8)|body[3];if(ipp>=0x0400){if(status)snprintf(status,status_len,"IPP rejected job (0x%04x)",ipp);goto done;}} }
    if(status)snprintf(status,status_len,"Job accepted by %s",u.host);
    ok=1;
done:
    if(s>=0)CloseSocket(s); if(SocketBase){CloseLibrary(SocketBase);SocketBase=NULL;} if(f)fclose(f); return ok;
}
