/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* Native Exec regression. Build with OAP.TestSelection. so no real UI receives
 * these synthetic destinations. No preference writes or networking occur. */
#include "oap_selection.h"
#include <exec/types.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
unsigned long __stack=65536;
static const char *first="ipp://192.0.2.1:631/ipp/print";
static const char *second="ipp://192.0.2.2:631/ipp/print";
static int checks,failures;
static FILE *report;
#define CHECK(x) do{checks++;if(!(x)){failures++;fprintf(report,"FAIL line %d: %s\n",__LINE__,#x);}}while(0)
int main(int argc,char **argv)
{
    OAPSelectionListener a={0},b={0};char uri[384],longuri[400],oldname[64];
    struct MsgPort *reply=NULL;struct Message foreign={0};unsigned long mask;int i;
    if(argc>1 && !strcmp(argv[1],"PUBLISH"))return oap_selection_publish(first)>=1?0:20;
    report=fopen("Work:OAPSelectionSmoke.txt","w");if(!report)return 20;
    CHECK(!strncmp(OAP_SELECTION_PREFIX,"OAP.TestSelection.",18));
    {char error[256];
     CHECK(oap_preferences_store("OAPSelectionSmokeValue",first,error,sizeof(error)));
     CHECK(oap_preferences_store("OAPSelectionSmokeValue",first,error,sizeof(error)));
     CHECK(oap_preferences_store("OAPSelectionSmokeValue",second,error,sizeof(error)));
     DeleteVar((STRPTR)"OAPSelectionSmokeValue",GVF_GLOBAL_ONLY|GVF_SAVE_VAR);
     DeleteFile((STRPTR)"ENVARC:OAPSelectionSmokeValue");
    }
    CHECK(oap_selection_publish(first)==0);
    CHECK(oap_selection_open(&a));CHECK(oap_selection_open(&b));
    if(!a.port||!b.port)goto out;
    CHECK(strcmp(a.name,b.name)!=0);mask=oap_selection_mask(&a)|oap_selection_mask(&b);
    SetSignal(0,mask);CHECK(oap_selection_publish(first)==2);
    CHECK((SetSignal(0,0)&mask)==mask);
    CHECK(oap_selection_receive(&a,uri,sizeof(uri)));CHECK(!strcmp(uri,first));
    CHECK(oap_selection_receive(&b,uri,sizeof(uri)));CHECK(!strcmp(uri,first));
    /* Exact regression: the same saved printer must still be a new event. */
    strcpy(uri,first);CHECK(oap_selection_publish(first)==2);
    CHECK(oap_selection_receive(&a,uri,sizeof(uri)));CHECK(!strcmp(uri,first));
    CHECK(oap_selection_receive(&b,uri,sizeof(uri)));CHECK(!strcmp(uri,first));
    CHECK(!oap_selection_receive(&a,uri,sizeof(uri)));
    /* Latest choice wins when two events arrive before the window wakes. */
    CHECK(oap_selection_publish(second)==2);CHECK(oap_selection_publish(first)==2);
    CHECK(oap_selection_receive(&a,uri,sizeof(uri)));CHECK(!strcmp(uri,first));
    CHECK(oap_selection_receive(&b,uri,sizeof(uri)));CHECK(!strcmp(uri,first));
    CHECK(oap_selection_publish("invalid")==-1);
    CHECK(oap_selection_publish("ipp://host\nunsafe")==-1);
    memset(longuri,'a',sizeof(longuri));memcpy(longuri,"ipp://",6);longuri[399]=0;
    CHECK(oap_selection_publish(longuri)==-1);CHECK(!oap_selection_receive(&a,uri,sizeof(uri)));
    CHECK(oap_selection_publish(first)==2);strcpy(uri,"unchanged");
    CHECK(!oap_selection_receive(&a,uri,4));CHECK(!strcmp(uri,"unchanged"));
    CHECK(oap_selection_receive(&b,uri,sizeof(uri)));
    /* The subscriber must not free an unrelated sender-owned message. */
    reply=CreateMsgPort();CHECK(reply!=NULL);
    if(reply){foreign.mn_ReplyPort=reply;foreign.mn_Length=sizeof(foreign);PutMsg(a.port,&foreign);
        CHECK(!oap_selection_receive(&a,uri,sizeof(uri)));CHECK(GetMsg(reply)==&foreign);DeleteMsgPort(reply);reply=NULL;}
    CHECK(oap_selection_publish(first)==2);strcpy(oldname,a.name);
    oap_selection_close(&a);CHECK(a.port==NULL);CHECK(oap_selection_mask(&a)==0);
    CHECK(oap_selection_publish(second)==1);CHECK(oap_selection_receive(&b,uri,sizeof(uri)));CHECK(!strcmp(uri,second));
    CHECK(oap_selection_open(&a));CHECK(strcmp(oldname,a.name)!=0);
    /* A separate process sends the selection while the receiver is inactive. */
    {BPTR in=Open((STRPTR)"NIL:",MODE_OLDFILE),out=Open((STRPTR)"NIL:",MODE_NEWFILE);LONG rc=-1;
     if(in&&out)rc=SystemTags((STRPTR)"C:OAPSelectionSmoke PUBLISH",SYS_Asynch,TRUE,SYS_Input,in,SYS_Output,out,NP_StackSize,65536,TAG_DONE);
     if(rc==-1){if(in)Close(in);if(out)Close(out);}CHECK(rc!=-1);
     for(i=0;i<100;i++){if(oap_selection_receive(&a,uri,sizeof(uri)))break;Delay(2);}
     CHECK(i<100);CHECK(!strcmp(uri,first));CHECK(oap_selection_receive(&b,uri,sizeof(uri)));}
    for(i=0;i<50;i++){CHECK(oap_selection_publish(first)==2);CHECK(oap_selection_receive(&a,uri,sizeof(uri)));CHECK(oap_selection_receive(&b,uri,sizeof(uri)));}
out:
    oap_selection_close(&a);oap_selection_close(&b);oap_selection_close(&a);
    CHECK(oap_selection_publish(first)==0);
    fprintf(report,"%s: %d native Exec selection checks; %d failures\n",failures?"FAIL":"PASS",checks,failures);
    fputs("Same URI, multiple windows, no tick/focus dependency, separate sender, queued cleanup.\nNo print job, network access or printer preference write.\n",report);
    fclose(report);return failures?20:0;
}
