/* SPDX-License-Identifier: BSD-2-Clause */
/* Guest request/worker smoke test. Invalid URI ensures no network print. */
#include "oav_jobs.h"
#include <exec/types.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
unsigned long __stack=65536;
int main(void)
{
 static OAVRequest r,back;static char req[512],msg[256],state[32];FILE *f;int i,ok=1;
 f=fopen("T:OAVRequestSmoke.req","w");if(!f)return 20;
 fputs("schema=3\naction=send\nsource=Work:OpenAmigaPrint.pdf\nuri=invalid\nprint_options=1\ncopies=2\nprint_paper=1\norientation=1\ncolor=0\nduplex=2\npage_start=2\npage_end=4\n",f);fclose(f);
 if(!oav_read_request("T:OAVRequestSmoke.req",&back)||!back.has_print_options||back.print.copies!=2||back.print.paper!=1||back.print.orientation!=1||back.print.color!=0||back.print.duplex!=2||back.print.page_start!=2||back.print.page_end!=4)ok=0;
 f=fopen("T:OAVRequestSmoke.req","w");if(!f)return 20;
 fputs("schema=2\naction=queue\nsource=Work:OpenAmigaPrint.pdf\npaper=0\n",f);fclose(f);
 if(!oav_read_request("T:OAVRequestSmoke.req",&back)||back.has_print_options)ok=0;
 if(oav_result_terminal("uploading")||oav_result_terminal("checking")||!oav_result_terminal("uncertain")||!oav_result_terminal("submitted"))ok=0;
 memset(&r,0,sizeof(r));oav_layout_defaults(&r.layout);strcpy(r.action,"send");strcpy(r.source,"Work:OpenAmigaPrint.pdf");strcpy(r.uri,"invalid");
 if(!oav_submit(&r,req,sizeof(req),msg,sizeof(msg)))ok=0;
 else {for(i=0;i<100;i++){if(oav_result(req,state,sizeof(state),msg,sizeof(msg))&&oav_result_terminal(state))break;Delay(5);}if(i==100||strcmp(state,"error"))ok=0;}
 f=fopen("Work:OAPWorkerSmoke.txt","w");if(f){fprintf(f,"%s: schema2/schema3, copies/layout/duplex/pages, terminal states and separate worker result\nRequest: %s\nResult: %s %s\nNo physical print was requested; deliberately invalid URI.\n",ok?"PASS":"FAIL",req,state,msg);fclose(f);}
 remove("T:OAVRequestSmoke.req");return ok?0:20;
}
