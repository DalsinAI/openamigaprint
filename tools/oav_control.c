/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* Test client for the public ARexx host. No document is executed as a script. */
#include <exec/types.h>
#include <exec/ports.h>
#include <rexx/storage.h>
#include <proto/exec.h>
#include <proto/rexxsyslib.h>
#include <stdio.h>
#include <string.h>
struct RxsLib *RexxSysBase;
unsigned long __stack=16384;
int main(int argc,char **argv)
{
 const char *in=argc>1?argv[1]:"Work:OAVTests/control.txt";
 const char *out=argc>2?argv[2]:"Work:OAVTests/control.log";
 FILE *f=NULL,*log=NULL;struct MsgPort *reply=NULL,*host;struct RexxMsg *m=NULL;
 char line[1024];int rc=20;
 RexxSysBase=(struct RxsLib *)OpenLibrary((STRPTR)"rexxsyslib.library",36);
 if(!RexxSysBase)goto done;
 f=fopen(in,"r");log=fopen(out,"w");reply=CreateMsgPort();if(!f||!log||!reply)goto done;
 while(fgets(line,sizeof(line),f)){
  char *e=strpbrk(line,"\r\n");if(e)*e=0;if(!line[0]||line[0]==';')continue;
  m=CreateRexxMsg(reply,(STRPTR)"rexx",(STRPTR)"OAVCONTROL");if(!m)goto done;
  m->rm_Action=RXCOMM|RXFF_RESULT;m->rm_Args[0]=CreateArgstring((STRPTR)line,strlen(line));if(!m->rm_Args[0])goto done;
  fprintf(log,"COMMAND %s\n",line);fflush(log);
  Forbid();host=FindPort((STRPTR)"OPENAMIGAVIEW");if(host)PutMsg(host,(struct Message *)m);Permit();
  if(!host){fputs("ERROR host not available\n",log);goto done;}
  WaitPort(reply);GetMsg(reply);
  fprintf(log,"RC %ld\n",(long)m->rm_Result1);
  if(m->rm_Result1==0&&m->rm_Result2){fprintf(log,"RESULT %s\n",(char *)m->rm_Result2);DeleteArgstring((UBYTE *)m->rm_Result2);}
  else fprintf(log,"ERROR %ld\n",(long)m->rm_Result2);
  fflush(log);DeleteArgstring(m->rm_Args[0]);m->rm_Args[0]=NULL;DeleteRexxMsg(m);m=NULL;
 }
 rc=0;
done:
 if(m){if(m->rm_Args[0])DeleteArgstring(m->rm_Args[0]);DeleteRexxMsg(m);}
 if(reply)DeleteMsgPort(reply);if(log)fclose(log);if(f)fclose(f);if(RexxSysBase)CloseLibrary((struct Library *)RexxSysBase);return rc;
}
