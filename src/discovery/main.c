/* SPDX-License-Identifier: BSD-2-Clause */
#include "oap_discovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
unsigned long __stack=65536;
int main(int argc,char **argv){if(argc==2)return oap_discover_run(argv[1]);if(argc==4&&!strcmp(argv[1],"--query")){OAPDiscovery *d=calloc(1,sizeof(*d));int ok;if(!d)return 20;d->count=1;d->printers[0].alive=1;strncpy(d->printers[0].uri,argv[2],sizeof(d->printers[0].uri)-1);strcpy(d->printers[0].label,"Manual printer");d->printers[0].secure=!strncmp(argv[2],"ipps://",7);if(!oap_net_start()){free(d);return 20;}ok=oap_query_pdf(argv[2],&d->printers[0].caps,d->printers[0].note,sizeof(d->printers[0].note));oap_discovery_save(argv[3],d,1,d->printers[0].note);printf("%s\n",d->printers[0].note);oap_net_stop();free(d);return ok?0:10;}fprintf(stderr,"Usage: OAPDiscover OUTPUT | OAPDiscover --query ipp://host:port/path OUTPUT\n");return 20;}
