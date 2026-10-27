/* Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT */
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

/* The paper sizes: those OpenPrint's driver makes (src/driver/openprint_driver.c, geometry) and OpenPrefs'
 * Printing page lists first, in its order. A4 and Letter keep 0 and 1, as a .job's paper= and the remembered
 * settings have them. src/core/paper.c has their words and sizes, src/core/ipp.c their IPP media names. */
enum { OAP_PAPER_A4 = 0, OAP_PAPER_LETTER = 1, OAP_PAPER_LEGAL = 2, OAP_PAPER_A3 = 3, OAP_PAPER_A5 = 4, OAP_PAPERS = 5 };
enum { OAP_PORTRAIT = 0, OAP_LANDSCAPE = 1 };
enum { OAP_MONO = 0, OAP_COLOR = 1 };
enum { OAP_SIMPLEX = 0, OAP_DUPLEX_LONG = 1, OAP_DUPLEX_SHORT = 2 };

typedef struct OAPUri {
    char host[256];
    unsigned short port;
    char path[384];
    int secure;                     /* ipps:// (IPP over TLS) */
} OAPUri;

void oap_job_defaults(OAPJobOptions *o);
/* The documents OpenPrint sends to printers: its own PDFs, and PostScript a program wrote itself (OAPSpooler
 * keeps that in the queue as .ps). PCL and other printer data are kept, never sent. */
#define OAP_FORMAT_PDF "application/pdf"
#define OAP_FORMAT_PS "application/postscript"
/* What a document is from its first bytes: OAP_FORMAT_PDF ("%PDF-"), OAP_FORMAT_PS ("%!", or "\004%!" after a
 * driver's end-of-job byte), or NULL for anything else. */
const char *oap_document_format(const unsigned char *head, size_t n);
/* A PostScript document's name for itself: the "%%Title:" line of its header comments (DSC), with a (string)'s
 * parentheses taken off, in the first n bytes. 1 when there is one. */
int oap_ps_title(const unsigned char *head, size_t n, char *out, size_t cap);
int oap_parse_ipp_uri(const char *uri, OAPUri *out);
/* The Print-Job request up to the document: oap_ipp_build_prefix's for a PDF, every job setting (copies, media,
 * orientation, colour, sides, pages). PostScript (format OAP_FORMAT_PS) sets its own paper and pages, so it gets
 * document-format, job-name and copies only. */
int oap_ipp_build_prefix(const OAPJobOptions *o, unsigned char *buf, size_t cap, size_t *out_len);
int oap_ipp_build_prefix_doc(const OAPJobOptions *o, const char *format, unsigned char *buf, size_t cap, size_t *out_len);
/* A paper's IPP media name (PWG 5101.1): iso_a4_210x297mm, na_letter_8.5x11in...; A4's for an unknown one. */
const char *oap_ipp_media(int paper);
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
/* The same for a PDF or a PostScript document, by its first bytes. PostScript goes only to a printer that lists
 * application/postscript among its formats (else a plain "not sent"), as it is, byte for byte, named by its
 * %%Title when it has one; anything else is not sent. */
int oap_ipp_submit_doc_ex(const char *path,const OAPJobOptions *o,char *status,size_t status_len,
                          OAPSendProgress notify,void *ctx);
#ifdef __amigaos__
int oap_run_print_dialog(const char *pdf_path,OAPJobOptions *o);
int oap_run_queue_window(void);
int oap_selected_printer(char *uri,size_t capacity);
#endif

#endif
