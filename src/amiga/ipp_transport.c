#include "oap.h"
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>
#ifdef OAP_WITH_AMISSL
#include <proto/amissl.h>
#include <proto/amisslmaster.h>
#include <amissl/amissl.h>
#include <libraries/amisslmaster.h>
#include <libraries/amissl.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

struct Library *SocketBase;
#ifdef OAP_WITH_AMISSL
struct Library *AmiSSLMasterBase;
struct Library *AmiSSLBase;
struct Library *AmiSSLExtBase;
typedef struct OAPTLS {
    SSL_CTX *ctx;
    SSL *ssl;
    int amissl_open;
} OAPTLS;
#else
typedef struct OAPTLS {
    int unused;
} OAPTLS;
#endif

static int send_all_plain(int s,const void *data,long len)
{
    const unsigned char *p=(const unsigned char *)data;
    while(len>0){
        long n=send(s,(char *)p,len,0);
        if(n<=0)return 0;
        p+=n;len-=n;
    }
    return 1;
}

#ifdef OAP_WITH_AMISSL
static int send_all_tls(SSL *ssl,const void *data,long len)
{
    const unsigned char *p=(const unsigned char *)data;
    while(len>0){
        int n=SSL_write(ssl,p,(int)len);
        if(n<=0)return 0;
        p+=n;len-=n;
    }
    return 1;
}
#endif
static long recv_transport(int s,OAPTLS *tls,void *buf,long len)
{
#ifdef OAP_WITH_AMISSL
    if(tls&&tls->ssl)return SSL_read(tls->ssl,buf,(int)len);
#else
    (void)tls;
#endif
    return recv(s,(char *)buf,len,0);
}

static long file_size(FILE *f)
{
    long p,n;
    p=ftell(f);
    if(p<0)return -1;
    if(fseek(f,0,SEEK_END))return -1;
    n=ftell(f);
    if(n>=0)fseek(f,p,SEEK_SET);
    return n;
}

#ifdef OAP_WITH_AMISSL
static void tls_cleanup(OAPTLS *tls)
{
    if(!tls)return;
    if(tls->ssl){
        SSL_shutdown(tls->ssl);
        SSL_free(tls->ssl);
        tls->ssl=NULL;
    }
    if(tls->ctx){
        SSL_CTX_free(tls->ctx);
        tls->ctx=NULL;
    }
    if(tls->amissl_open){
        CloseAmiSSL();
        tls->amissl_open=0;
        AmiSSLBase=NULL;
        AmiSSLExtBase=NULL;
    }
    if(AmiSSLMasterBase){
        CloseLibrary(AmiSSLMasterBase);
        AmiSSLMasterBase=NULL;
    }
}

static int tls_connect(OAPTLS *tls,int s,const char *host,
                       char *status,size_t status_len)
{
    long verify_result;
    memset(tls,0,sizeof(*tls));

    AmiSSLMasterBase=OpenLibrary((STRPTR)"amisslmaster.library",
                                 AMISSLMASTER_MIN_VERSION);
    if(!AmiSSLMasterBase){
        if(status)snprintf(status,status_len,"AmiSSL v5 runtime not available");
        return 0;
    }
    if(OpenAmiSSLTags(AMISSL_CURRENT_VERSION,
                      AmiSSL_UsesOpenSSLStructs,FALSE,
                      AmiSSL_GetAmiSSLBase,(ULONG)&AmiSSLBase,
                      AmiSSL_GetAmiSSLExtBase,(ULONG)&AmiSSLExtBase,
                      AmiSSL_SocketBase,(ULONG)SocketBase,
                      AmiSSL_ErrNoPtr,(ULONG)&errno,
                      TAG_DONE)!=0){
        if(status)snprintf(status,status_len,"Cannot initialize AmiSSL");
        tls_cleanup(tls);
        return 0;
    }
    tls->amissl_open=1;

    if(!OPENSSL_init_ssl(OPENSSL_INIT_SSL_DEFAULT,NULL)){
        if(status)snprintf(status,status_len,"AmiSSL initialization failed");
        tls_cleanup(tls);
        return 0;
    }
    tls->ctx=SSL_CTX_new(TLS_client_method());
    if(!tls->ctx){
        if(status)snprintf(status,status_len,"Cannot create TLS context");
        tls_cleanup(tls);
        return 0;
    }
    if(SSL_CTX_set_default_verify_paths(tls->ctx)!=1){
        if(status)snprintf(status,status_len,"Cannot load AmiSSL trust store");
        tls_cleanup(tls);
        return 0;
    }
    SSL_CTX_set_verify(tls->ctx,SSL_VERIFY_PEER,NULL);

    tls->ssl=SSL_new(tls->ctx);
    if(!tls->ssl){
        if(status)snprintf(status,status_len,"Cannot create TLS session");
        tls_cleanup(tls);
        return 0;
    }
    if(SSL_set_fd(tls->ssl,s)!=1 ||
       SSL_set_tlsext_host_name(tls->ssl,host)!=1 ||
       SSL_set1_host(tls->ssl,host)!=1){
        if(status)snprintf(status,status_len,"Cannot configure TLS hostname");
        tls_cleanup(tls);
        return 0;
    }
    if(SSL_connect(tls->ssl)!=1){
        verify_result=SSL_get_verify_result(tls->ssl);
        if(status&&verify_result!=X509_V_OK)
            snprintf(status,status_len,"TLS certificate rejected (%ld)",verify_result);
        else if(status)
            snprintf(status,status_len,"TLS handshake failed");
        tls_cleanup(tls);
        return 0;
    }
    verify_result=SSL_get_verify_result(tls->ssl);
    if(verify_result!=X509_V_OK){
        if(status)snprintf(status,status_len,
                           "TLS certificate rejected (%ld)",verify_result);
        tls_cleanup(tls);
        return 0;
    }
    return 1;
}
#else
static void tls_cleanup(OAPTLS *tls)
{
    (void)tls;
}

static int tls_connect(OAPTLS *tls,int s,const char *host,
                       char *status,size_t status_len)
{
    (void)tls;(void)s;(void)host;
    if(status)snprintf(status,status_len,"IPPS support was not built");
    return 0;
}
#endif

static int send_all_transport(int s,OAPTLS *tls,const void *data,long len)
{
#ifdef OAP_WITH_AMISSL
    if(tls&&tls->ssl)return send_all_tls(tls->ssl,data,len);
#else
    (void)tls;
#endif
    return send_all_plain(s,data,len);
}
int oap_ipp_submit_pdf(const char *pdf,const OAPJobOptions *o,
                       char *status,size_t status_len)
{
    OAPUri u;
    struct hostent *he;
    struct sockaddr_in sa;
    FILE *f=NULL;
    unsigned char prefix[2048],chunk[8192],resp[4096];
    size_t plen=0;
    char hdr[1024];
    long flen,n,rn=0;
    int s=-1,http=0,ok=0;
    OAPTLS tls;

    memset(&tls,0,sizeof(tls));
    if(status&&status_len)snprintf(status,status_len,"Preparing job...");
    if(!oap_parse_ipp_uri(o->printer_uri,&u)){
        if(status)snprintf(status,status_len,"Invalid ipp:// or ipps:// URI");
        return 0;
    }
    f=fopen(pdf,"rb");
    if(!f){
        if(status)snprintf(status,status_len,"Cannot open PDF spool file");
        return 0;
    }
    flen=file_size(f);
    if(flen<0||!oap_ipp_build_prefix(o,prefix,sizeof(prefix),&plen)){
        if(status)snprintf(status,status_len,"Cannot prepare IPP job");
        goto done;
    }

    SocketBase=OpenLibrary((STRPTR)"bsdsocket.library",4);
    if(!SocketBase){
        if(status)snprintf(status,status_len,
                           "bsdsocket.library v4 not available");
        goto done;
    }
    he=gethostbyname((STRPTR)u.host);
    if(!he||!he->h_addr_list||!he->h_addr_list[0]){
        if(status)snprintf(status,status_len,"Cannot resolve printer host");
        goto done;
    }
    memset(&sa,0,sizeof(sa));
    sa.sin_family=AF_INET;
    sa.sin_port=htons(u.port);
    memcpy(&sa.sin_addr,he->h_addr_list[0],sizeof(sa.sin_addr));

    s=socket(AF_INET,SOCK_STREAM,0);
    if(s<0){
        if(status)snprintf(status,status_len,"Cannot create TCP socket");
        goto done;
    }
    if(connect(s,(struct sockaddr *)&sa,sizeof(sa))<0){
        if(status)snprintf(status,status_len,"Cannot connect to printer");
        goto done;
    }

    if(u.secure){
        if(status)snprintf(status,status_len,"Establishing secure IPPS...");
        if(!tls_connect(&tls,s,u.host,status,status_len))goto done;
    }
    n=snprintf(hdr,sizeof(hdr),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Content-Type: application/ipp\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n"
        "User-Agent: OpenAmigaPrint/0.2\r\n\r\n",
        u.path,u.host,(unsigned)u.port,(unsigned long)(plen+flen));

    if(n<=0||n>=(long)sizeof(hdr)||
       !send_all_transport(s,&tls,hdr,n)||
       !send_all_transport(s,&tls,prefix,(long)plen)){
        if(status)snprintf(status,status_len,"IPP request write failed");
        goto done;
    }
    while((n=(long)fread(chunk,1,sizeof(chunk),f))>0){
        if(!send_all_transport(s,&tls,chunk,n)){
            if(status)snprintf(status,status_len,"PDF upload interrupted");
            goto done;
        }
    }

    rn=recv_transport(s,&tls,resp,sizeof(resp)-1);
    if(rn<=0){
        if(status)snprintf(status,status_len,"Printer returned no response");
        goto done;
    }
    resp[rn]=0;
    if(sscanf((char *)resp,"HTTP/%*s %d",&http)!=1||
       http<200||http>=300){
        if(status)snprintf(status,status_len,"Printer HTTP error %d",http);
        goto done;
    }
    {
        unsigned char *body=(unsigned char *)strstr((char *)resp,"\r\n\r\n");
        if(body&&body+8<resp+rn){
            unsigned ipp;
            body+=4;
            ipp=((unsigned)body[2]<<8)|body[3];
            if(ipp>=0x0400){
                if(status)snprintf(status,status_len,
                                   "IPP rejected job (0x%04x)",ipp);
                goto done;
            }
        }
    }
    if(status)snprintf(status,status_len,
                       u.secure?"Secure job accepted by %s":"Job accepted by %s",
                       u.host);
    ok=1;
done:
    tls_cleanup(&tls);
    if(s>=0)CloseSocket(s);
    if(SocketBase){
        CloseLibrary(SocketBase);
        SocketBase=NULL;
    }
    if(f)fclose(f);
    return ok;
}
