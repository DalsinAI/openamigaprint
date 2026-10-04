#define _GNU_SOURCE
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <dlfcn.h>
ssize_t sendto(int s,const void *buf,size_t n,int flags,const struct sockaddr *to,socklen_t len)
{
    const struct sockaddr_in *a=(const struct sockaddr_in *)to;
    ssize_t (*real_sendto)(int,const void *,size_t,int,const struct sockaddr *,socklen_t);
    if(to && len>=sizeof(*a) && a->sin_family==AF_INET &&
       a->sin_addr.s_addr==htonl(0xe00000fb) && a->sin_port==htons(5353)){
        errno=ENETUNREACH;return -1;
    }
    real_sendto=dlsym(RTLD_NEXT,"sendto");
    return real_sendto(s,buf,n,flags,to,len);
}
