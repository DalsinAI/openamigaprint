/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#ifndef OAP_DISCOVERY_H
#define OAP_DISCOVERY_H
#include <stddef.h>
#include <stdint.h>
#define OAP_DISC_MAX 24
#define OAP_HOST_MAX 48
#define OAP_HTTP_MAX 196608U
#define OAP_BODY_MAX 131072U
#define OAP_DISC_SELECTION "ENV:OpenAmigaPrint/PrinterURI"
#define OAP_DISC_SAVED "ENVARC:OpenAmigaPrint/PrinterURI"
enum { OAP_PDF_UNKNOWN=0, OAP_PDF_YES=1, OAP_PDF_NO=2 };
typedef struct OAPName { unsigned char wire[256]; size_t len; } OAPName;
typedef struct OAPCaps { int pdf, accepting, state, color, duplex; unsigned status; uint32_t job_id; char name[128], model[128], location[128], reasons[256]; } OAPCaps;
typedef struct OAPDiscovered { OAPName service, target; char label[128], host[256], ip[16], path[256], uri[640], note[256]; unsigned port; int secure, have_srv, have_txt, alive; OAPCaps caps; } OAPDiscovered;
typedef struct OAPHost { OAPName name; char ip[16]; } OAPHost;
typedef struct OAPDiscovery { OAPDiscovered printers[OAP_DISC_MAX]; OAPHost hosts[OAP_HOST_MAX]; size_t count, host_count; } OAPDiscovery;
int oap_dns_name_text(const char *, OAPName *);
size_t oap_dns_query(const OAPName *, unsigned, uint16_t, unsigned char *, size_t);
int oap_dns_packet(OAPDiscovery *, const unsigned char *, size_t);
void oap_discovery_resolve(OAPDiscovery *);
size_t oap_ipp_query_request(const char *, uint32_t, unsigned char *, size_t);
int oap_ipp_parse_caps(const unsigned char *, size_t, uint32_t, OAPCaps *);
int oap_http_response(const unsigned char *, size_t, int, unsigned char *, size_t, size_t *, int *);
int oap_pdf_eligible(const OAPDiscovered *);
int oap_discovery_save(const char *, const OAPDiscovery *, int, const char *);
int oap_discovery_load(const char *, OAPDiscovery *, int *, char *, size_t);
int oap_net_start(void);
int oap_net_connect(const char *,unsigned);
int oap_net_write(int,const void *,size_t);
void oap_net_stop(void);
void oap_net_close(int);
int oap_query_pdf(const char *, OAPCaps *, char *, size_t);
int oap_discover_run(const char *);
int oap_receive_ipp(int, unsigned char *, size_t, size_t *, char *, size_t);
#endif
