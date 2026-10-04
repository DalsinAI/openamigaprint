/* SPDX-License-Identifier: BSD-2-Clause */
#include "oav_core.h"
#include <stdio.h>
static int row(void *ctx,unsigned long y,unsigned char *p,size_t n)
{size_t i;(void)ctx;for(i=0;i<n;i+=3){p[i]=(unsigned char)(y*4);p[i+1]=(unsigned char)i;p[i+2]=180;}return 1;}
int main(void)
{
 OAVLayout s;char error[128];FILE *f;int ok;
 oav_layout_defaults(&s);f=fopen("Work:OAVTests/core-v3.pdf","wb");if(!f){puts("FAIL opening output");return 20;}
 ok=oav_pdf_rgb(f,96,64,&s,row,NULL,error,sizeof(error));if(fclose(f))ok=0;
 printf("%s native RGB export: %s\n",ok?"PASS":"FAIL",error);return ok?0:20;
}
