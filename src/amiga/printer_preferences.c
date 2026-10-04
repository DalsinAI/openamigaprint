/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#include "oap_selection.h"
#include <dos/dos.h>
#include <dos/var.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>

/* ENV: may be a variable/cache handler rather than an ordinary filesystem.
 * Use DOS's environment API instead of delete+rename of ENV: files. SetVar's
 * result alone is insufficient on third-party handlers, so read back both
 * the global value and its reboot-persistent archive before publishing. */
int oap_preferences_store(const char *name,const char *uri,char *error,size_t cap)
{
    char current[OAP_SELECTION_URI_MAX],path[256],archived[OAP_SELECTION_URI_MAX];
    LONG got;FILE *f;size_t n;
    if(!name||!uri||strchr(name,':')||strlen(name)>200 ||
       strncmp(uri,"ipp://",6)||(n=strlen(uri))>=sizeof(current)){
        snprintf(error,cap,"Invalid printer preference");return 0;
    }
    if(!SetVar((STRPTR)name,(STRPTR)uri,(LONG)n,GVF_GLOBAL_ONLY|GVF_SAVE_VAR)){
        snprintf(error,cap,"Cannot save printer preference (DOS error %ld)",(long)IoErr());return 0;
    }
    got=GetVar((STRPTR)name,(STRPTR)current,sizeof(current),GVF_GLOBAL_ONLY);
    if(got<0||(size_t)got!=n||memcmp(current,uri,n)){
        snprintf(error,cap,"ENV: printer verification failed (DOS error %ld)",(long)IoErr());return 0;
    }
    snprintf(path,sizeof(path),"ENVARC:%s",name);
    f=fopen(path,"rb");
    if(!f){snprintf(error,cap,"Cannot verify saved printer in ENVARC:");return 0;}
    n=fread(archived,1,sizeof(archived),f);
    if(ferror(f)){fclose(f);snprintf(error,cap,"Cannot read saved printer in ENVARC:");return 0;}
    fclose(f);
    if(n!=strlen(uri)||memcmp(archived,uri,n)){
        snprintf(error,cap,"ENVARC: printer verification failed");return 0;
    }
    if(cap)error[0]=0;
    return 1;
}
