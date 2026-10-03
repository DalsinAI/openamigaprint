/* SPDX-License-Identifier: BSD-2-Clause */
#include "oap.h"
#include "oap_discovery.h"
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
extern struct Library *SocketBase;
int oap_ipp_submit_pdf(const char *pdf,const OAPJobOptions *o,char *status,size_t cap)
{
 OAPUri u;OAPCaps capabilities,reply;FILE *f=NULL;
 unsigned char prefix[2048],chunk[8192],*body=NULL;size_t plen=0,bn=0,n;
 char header[1024],note[256];long flen,hn;int socket=-1,ok=0;
 if(!status||!cap)return 0;status[0]=0;
 if(!oap_parse_ipp_uri(o->printer_uri,&u)){snprintf(status,cap,"Invalid ipp:// URI; select a verified printer");return 0;}
 f=fopen(pdf,"rb");if(!f){snprintf(status,cap,"Cannot open PDF spool file");return 0;}
 if(fseek(f,0,SEEK_END)||(flen=ftell(f))<5||fseek(f,0,SEEK_SET)){snprintf(status,cap,"Cannot measure PDF");goto done;}
 if(fread(chunk,1,5,f)!=5||memcmp(chunk,"%PDF-",5)||fseek(f,0,SEEK_SET)){snprintf(status,cap,"Source is not a PDF document");goto done;}
 if(!oap_net_start()){snprintf(status,cap,"bsdsocket.library is not available");goto done;}
 if(!oap_query_pdf(o->printer_uri,&capabilities,note,sizeof(note))||capabilities.pdf!=OAP_PDF_YES){snprintf(status,cap,"PDF not confirmed: %.180s",note);goto done;}
 if(capabilities.accepting==0){snprintf(status,cap,"Printer is not accepting jobs");goto done;}
 if(!oap_ipp_build_prefix(o,prefix,sizeof(prefix),&plen)){snprintf(status,cap,"Cannot encode IPP print job");goto done;}
 socket=oap_net_connect(u.host,u.port);if(socket<0){snprintf(status,cap,"Printer connection failed");goto done;}
 hn=snprintf(header,sizeof(header),"POST %s HTTP/1.1\r\nHost: %s:%u\r\nContent-Type: application/ipp\r\nContent-Length: %lu\r\nConnection: close\r\nUser-Agent: OpenAmigaPrint/0.2\r\n\r\n",u.path,u.host,(unsigned)u.port,(unsigned long)(plen+flen));
 if(hn<0||hn>=(long)sizeof(header)||!oap_net_write(socket,header,(size_t)hn)||!oap_net_write(socket,prefix,plen)){snprintf(status,cap,"IPP request write failed");goto done;}
 while((n=fread(chunk,1,sizeof(chunk),f))!=0)if(!oap_net_write(socket,chunk,n)){snprintf(status,cap,"PDF upload failed; do not retry without checking printer jobs");goto done;}
 if(ferror(f)){snprintf(status,cap,"PDF file read failed");goto done;}
 body=malloc(OAP_BODY_MAX);if(!body){snprintf(status,cap,"Cannot allocate response buffer");goto done;}
 note[0]=0;if(!oap_receive_ipp(socket,body,OAP_BODY_MAX,&bn,note,sizeof(note))){snprintf(status,cap,"Submission uncertain: %.170s",note);goto done;}
 if(!oap_ipp_parse_caps(body,bn,1,&reply)||!reply.job_id){snprintf(status,cap,"No complete successful IPP job-id response; check printer queue");goto done;}
 snprintf(status,cap,"Accepted job %lu by %.80s; not yet confirmed printed",(unsigned long)reply.job_id,u.host);ok=1;
done:if(socket>=0)CloseSocket(socket);oap_net_stop();free(body);if(f)fclose(f);return ok;
}
