/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#include "oap.h"
#include "oap_discovery.h"
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
int oap_selected_printer(char *uri,size_t cap){FILE *f;size_t n;f=fopen(OAP_DISC_SELECTION,"r");if(!f)f=fopen(OAP_DISC_SAVED,"r");if(!f)f=fopen(OAP_DISC_SELECTION_OLD,"r");if(!f)f=fopen(OAP_DISC_SAVED_OLD,"r");if(!f)return 0;if(!fgets(uri,cap,f)){fclose(f);return 0;}fclose(f);uri[strcspn(uri,"\r\n")]=0;n=strlen(uri);return n>6&&!strncmp(uri,"ipp://",6);}
int oap_launch_printer_browser(void){BPTR in=Open((STRPTR)"NIL:",MODE_OLDFILE),out=Open((STRPTR)"NIL:",MODE_NEWFILE);LONG rc;if(!in||!out){if(in)Close(in);if(out)Close(out);return 0;}rc=SystemTags((STRPTR)"C:OAPPrinters",SYS_Asynch,TRUE,SYS_Input,in,SYS_Output,out,NP_StackSize,65536,TAG_DONE);if(rc==-1){Close(in);Close(out);return 0;}return 1;}
