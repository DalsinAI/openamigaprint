/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#include "oap.h"
#include "oap_discovery.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct SendWorkspace {
    OAPUri uri;
    OAPCaps capabilities, reply;
    OAPJobOptions effective;
    unsigned char prefix[2048], chunk[8192];
    char header[1024], note[256];
} SendWorkspace;

static int progress(OAPSendProgress fn, void *ctx, const char *stage,
                    unsigned long sent, unsigned long total)
{
    return !fn || fn(ctx, stage, sent, total);
}

int oap_ipp_submit_pdf_ex(const char *pdf, const OAPJobOptions *o,
                        char *status, size_t cap,
                        OAPSendProgress notify, void *ctx)
{
    SendWorkspace *s = NULL;
    FILE *f = NULL;
    unsigned char *body = NULL;
    size_t plen = 0, bn = 0, n;
    long flen = 0, hn;
    unsigned long uploaded = 0;
    int fd = -1, result = OAP_SEND_ERROR, attempted = 0;
    if (!pdf || !o || !status || !cap) return OAP_SEND_ERROR;
    status[0] = 0;
    s = calloc(1, sizeof(*s));
    if (!s) { snprintf(status, cap, "Cannot allocate print workspace"); return OAP_SEND_ERROR; }
    if (!oap_parse_ipp_uri(o->printer_uri, &s->uri)) {
        snprintf(status, cap, "Invalid ipp:// URI; select a verified printer"); goto done;
    }
    if (!progress(notify, ctx, "preparing", 0, 0)) goto cancelled;
    f = fopen(pdf, "rb");
    if (!f) { snprintf(status, cap, "Cannot open PDF spool file"); goto done; }
    if (fseek(f, 0, SEEK_END) || (flen = ftell(f)) < 5 || fseek(f, 0, SEEK_SET)) {
        snprintf(status, cap, "Cannot measure PDF"); goto done;
    }
    if (fread(s->chunk, 1, 5, f) != 5 || memcmp(s->chunk, "%PDF-", 5) || fseek(f, 0, SEEK_SET)) {
        snprintf(status, cap, "Source is not a PDF document"); goto done;
    }
    if (!progress(notify, ctx, "checking", 0, (unsigned long)flen)) goto cancelled;
    if (!oap_net_start()) { snprintf(status, cap, "bsdsocket.library is not available"); goto done; }
    if (!oap_query_pdf(o->printer_uri, &s->capabilities, s->note, sizeof(s->note)) ||
        s->capabilities.pdf != OAP_PDF_YES) {
        snprintf(status, cap, "PDF not confirmed: %.180s", s->note); goto done;
    }
    if (s->capabilities.accepting == 0) { snprintf(status, cap, "Printer is not accepting jobs"); goto done; }
    s->effective = *o;
    if (!s->capabilities.color) s->effective.color = OAP_MONO;
    if (s->effective.duplex != OAP_SIMPLEX && !s->capabilities.duplex) {
        snprintf(status, cap, "Printer has not confirmed duplex; choose one-sided"); goto done;
    }
    if (!oap_ipp_build_prefix(&s->effective, s->prefix, sizeof(s->prefix), &plen)) {
        snprintf(status, cap, "Cannot encode IPP print job"); goto done;
    }
    if (!progress(notify, ctx, "connecting", 0, (unsigned long)flen)) goto cancelled;
    fd = oap_net_connect(s->uri.host, s->uri.port);
    if (fd < 0) { snprintf(status, cap, "Printer connection failed"); goto done; }
    hn = snprintf(s->header, sizeof(s->header),
        "POST %s HTTP/1.1\r\nHost: %s:%u\r\nContent-Type: application/ipp\r\n"
        "Content-Length: %lu\r\nConnection: close\r\nUser-Agent: OpenPrint/0.2\r\n\r\n",
        s->uri.path, s->uri.host, (unsigned)s->uri.port, (unsigned long)(plen + flen));
    if (hn < 0 || hn >= (long)sizeof(s->header)) {
        snprintf(status, cap, "IPP request header too large"); goto done;
    }
    if (!progress(notify, ctx, "uploading", 0, (unsigned long)flen)) goto cancelled;
    /* Once transmission starts, a lost reply must never become an automatic retry. */
    attempted = 1;
    if (!oap_net_write(fd, s->header, (size_t)hn) || !oap_net_write(fd, s->prefix, plen)) {
        snprintf(status, cap, "Submission uncertain: IPP request write failed"); goto done;
    }
    while ((n = fread(s->chunk, 1, sizeof(s->chunk), f)) != 0) {
        if (!progress(notify, ctx, "uploading", uploaded, (unsigned long)flen)) goto cancelled;
        if (!oap_net_write(fd, s->chunk, n)) {
            snprintf(status, cap, "Submission uncertain: PDF upload interrupted; check printer"); goto done;
        }
        uploaded += (unsigned long)n;
    }
    if (ferror(f) || uploaded != (unsigned long)flen) {
        snprintf(status, cap, "Submission uncertain: PDF read length changed"); goto done;
    }
    if (!progress(notify, ctx, "awaiting-reply", uploaded, (unsigned long)flen)) goto cancelled;
    body = malloc(OAP_BODY_MAX);
    if (!body) { snprintf(status, cap, "Submission uncertain: cannot allocate response buffer"); goto done; }
    s->note[0] = 0;
    if (!oap_receive_ipp(fd, body, OAP_BODY_MAX, &bn, s->note, sizeof(s->note))) {
        snprintf(status, cap, "Submission uncertain: %.170s", s->note); goto done;
    }
    if (!oap_ipp_parse_caps(body, bn, 1, &s->reply) || !s->reply.job_id) {
        snprintf(status, cap, "Submission uncertain: no successful IPP job-id; check printer"); goto done;
    }
    snprintf(status, cap, "Accepted job %lu by %.80s; not yet confirmed printed",
             (unsigned long)s->reply.job_id, s->uri.host);
    result = OAP_SEND_ACCEPTED;
    goto done;
cancelled:
    result = attempted ? OAP_SEND_UNCERTAIN : OAP_SEND_CANCELLED;
    snprintf(status, cap, attempted ? "Upload stopped; printer outcome uncertain. Check printer before retrying."
                                   : "Cancelled before sending a print job");
done:
    if (attempted && result == OAP_SEND_ERROR) result = OAP_SEND_UNCERTAIN;
    if (fd >= 0) oap_net_close(fd);
    oap_net_stop();
    free(body);
    if (f) fclose(f);
    free(s);
    return result;
}

int oap_ipp_submit_pdf(const char *pdf, const OAPJobOptions *o, char *status, size_t cap)
{
    return oap_ipp_submit_pdf_ex(pdf, o, status, cap, NULL, NULL) == OAP_SEND_ACCEPTED;
}
