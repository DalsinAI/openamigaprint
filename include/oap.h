/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#ifndef OAP_H
#define OAP_H
#include <stddef.h>

typedef struct OAPJobOptions {
    char printer_uri[384];
    char job_name[96];
    int copies;
    int paper;
    int orientation;
    int color;
    int duplex;
    int page_start;
    int page_end;
} OAPJobOptions;

enum { OAP_PAPER_A4 = 0, OAP_PAPER_LETTER = 1 };
enum { OAP_PORTRAIT = 0, OAP_LANDSCAPE = 1 };
enum { OAP_MONO = 0, OAP_COLOR = 1 };
enum { OAP_SIMPLEX = 0, OAP_DUPLEX_LONG = 1, OAP_DUPLEX_SHORT = 2 };

typedef struct OAPUri {
    char host[256];
    unsigned short port;
    char path[384];
} OAPUri;

void oap_job_defaults(OAPJobOptions *o);
int oap_parse_ipp_uri(const char *uri, OAPUri *out);
int oap_ipp_build_prefix(const OAPJobOptions *o, unsigned char *buf, size_t cap, size_t *out_len);
int oap_pdf_write_demo(const char *path, const char *title);
enum { OAP_SEND_ERROR=0, OAP_SEND_ACCEPTED=1, OAP_SEND_UNCERTAIN=-1, OAP_SEND_CANCELLED=-2 };
/* Progress of a print job: stage is "preparing", "checking", "connecting",
 * "uploading" or "awaiting-reply". Returning 0 stops the job. */
typedef int (*OAPSendProgress)(void *ctx,const char *stage,unsigned long sent,unsigned long total);
/* Sends the PDF at `pdf_path` to o->printer_uri with IPP Print-Job. Once
 * any of it was sent, a failure is OAP_SEND_UNCERTAIN, never an automatic
 * retry. `status` says what happened in words. */
int oap_ipp_submit_pdf_ex(const char *pdf_path,const OAPJobOptions *o,char *status,size_t status_len,
                          OAPSendProgress notify,void *ctx);
#ifdef __amigaos__
int oap_run_print_dialog(const char *pdf_path,OAPJobOptions *o);
int oap_run_queue_window(void);
int oap_selected_printer(char *uri,size_t capacity);
#endif

#endif
