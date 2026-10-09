/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* IPP Print-Job of a PDF to an ipp:// or ipps:// printer (include/oap.h). */
#include "oap.h"
#include "oap_net.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct SendWorkspace {
    OAPUri uri;
    OAPCaps capabilities, reply;
    OAPJobOptions effective;
    OAPTlsInfo tls;
    OAPHttpReply http;
    unsigned char prefix[2048], chunk[8192];
    char note[256], authorization[1024];
} SendWorkspace;

static int progress(OAPSendProgress fn, void *ctx, const char *stage,
                    unsigned long sent, unsigned long total)
{
    return !fn || fn(ctx, stage, sent, total);
}

/* Why an ipps:// printer may not have the job, in words. */
static void untrusted(const OAPTlsInfo *tls, char *status, size_t cap)
{
    if (tls->verdict == OAP_CERT_NOTLS)
        snprintf(status, cap, "%s", "This printer uses IPPS (encrypted IPP), which needs opentls.library or AmiSSL 5. "
                 "Install one, or use the printer's ipp:// address.");
    else if (tls->verdict == OAP_CERT_ASK)
        snprintf(status, cap, "This printer's certificate isn't trusted yet: in Printers and Queue, select it and "
                 "click Use for printing to check and trust it");
    else if (tls->verdict == OAP_CERT_CHANGED)
        snprintf(status, cap, "This printer's certificate has changed since you trusted it: check it in Printers "
                 "and Queue before printing");
    else
        snprintf(status, cap, "Not sent: %s", oap_cert_words(tls->verdict));
}

int oap_ipp_submit_pdf_ex(const char *pdf, const OAPJobOptions *o,
                        char *status, size_t cap,
                        OAPSendProgress notify, void *ctx)
{
    SendWorkspace *s = NULL;
    FILE *f = NULL;
    unsigned char *body = NULL;
    size_t plen = 0, bn = 0, n;
    long flen = 0;
    unsigned long uploaded = 0;
    OAPConn *conn = NULL;
    int result = OAP_SEND_ERROR, attempted = 0, tries;
    if (!pdf || !o || !status || !cap) return OAP_SEND_ERROR;
    status[0] = 0;
    s = calloc(1, sizeof(*s));
    if (!s) { snprintf(status, cap, "Cannot allocate print workspace"); return OAP_SEND_ERROR; }
    if (!oap_parse_ipp_uri(o->printer_uri, &s->uri)) {
        snprintf(status, cap, "Invalid ipp:// or ipps:// URI; select a verified printer"); goto done;
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
    if (!oap_query_pdf(o->printer_uri, &s->capabilities, &s->tls, s->note, sizeof(s->note))) {
        if (s->uri.secure && s->tls.verdict == OAP_CERT_NOTLS)
            untrusted(&s->tls, status, cap);
        else
            snprintf(status, cap, "PDF not confirmed: %.180s", s->note);
        goto done;
    }
    if (s->uri.secure && !oap_cert_usable(s->tls.verdict)) { untrusted(&s->tls, status, cap); goto done; }
    if (s->capabilities.pdf != OAP_PDF_YES) { snprintf(status, cap, "PDF not confirmed: %.180s", s->note); goto done; }
    if (s->capabilities.accepting == 0) { snprintf(status, cap, "Printer is not accepting jobs"); goto done; }
    s->effective = *o;
    if (!s->capabilities.color) s->effective.color = OAP_MONO;
    if (s->effective.duplex != OAP_SIMPLEX && !s->capabilities.duplex) {
        snprintf(status, cap, "Printer has not confirmed duplex; choose one-sided"); goto done;
    }
    if (!oap_ipp_build_prefix(&s->effective, s->prefix, sizeof(s->prefix), &plen)) {
        snprintf(status, cap, "Cannot encode IPP print job"); goto done;
    }
    body = malloc(OAP_BODY_MAX);
    if (!body) { snprintf(status, cap, "Cannot allocate response buffer"); goto done; }
    /* Twice at most: the second time only after the printer answered 401
     * (it did not take the job), with the login it asked for. */
    for (tries = 0; tries < 2; tries++) {
        const char *authorization = NULL;
        if (!progress(notify, ctx, "connecting", 0, (unsigned long)flen)) goto cancelled;
        oap_conn_close(conn);
        conn = oap_conn_open(&s->uri, OAP_TLS_VERIFY, &s->tls, s->note, sizeof(s->note));
        if (!conn) {
            if (s->uri.secure && s->tls.verdict != OAP_CERT_FAILED && s->tls.verdict != OAP_CERT_NONE)
                untrusted(&s->tls, status, cap);
            else
                snprintf(status, cap, "%s", s->note);
            goto done;
        }
        if (tries) {
            if (!oap_ipp_login(conn, &s->uri, &s->http, 1, s->authorization, sizeof(s->authorization),
                               s->note, sizeof(s->note))) {
                snprintf(status, cap, "Not printed: %.200s", s->note);
                result = OAP_SEND_ERROR;
                attempted = 0;                 /* the 401 said plainly that nothing was taken */
                goto done;
            }
            authorization = s->authorization;
        }
        if (fseek(f, 0, SEEK_SET)) { snprintf(status, cap, "Cannot read the PDF again"); goto done; }
        uploaded = 0;
        if (!progress(notify, ctx, "uploading", 0, (unsigned long)flen)) goto cancelled;
        /* Once transmission starts, a lost reply must never become an automatic retry. */
        attempted = 1;
        if (!oap_http_post(conn, &s->uri, (unsigned long)(plen + flen), authorization) ||
            !oap_conn_write(conn, s->prefix, plen, 5)) {
            snprintf(status, cap, "Submission uncertain: IPP request write failed"); goto done;
        }
        while ((n = fread(s->chunk, 1, sizeof(s->chunk), f)) != 0) {
            if (!progress(notify, ctx, "uploading", uploaded, (unsigned long)flen)) goto cancelled;
            if (!oap_conn_write(conn, s->chunk, n, 5)) {
                snprintf(status, cap, "Submission uncertain: PDF upload interrupted; check printer"); goto done;
            }
            uploaded += (unsigned long)n;
        }
        if (ferror(f) || uploaded != (unsigned long)flen) {
            snprintf(status, cap, "Submission uncertain: PDF read length changed"); goto done;
        }
        if (!progress(notify, ctx, "awaiting-reply", uploaded, (unsigned long)flen)) goto cancelled;
        s->note[0] = 0;
        /* A printer may take a while to answer a whole document: longer than a query. */
        if (oap_receive_ipp(conn, body, OAP_BODY_MAX, &bn, 20, &s->http, s->note, sizeof(s->note)))
            break;
        if (s->http.status == 401 && !tries) {
            attempted = 0;                     /* refused, not taken: safe to send again */
            continue;
        }
        if (s->http.status == 401) {
            snprintf(status, cap, "Not printed: the printer refused the user name and password in "
                     "ENVARC:OpenPrint/Logins");
            attempted = 0;
            goto done;
        }
        snprintf(status, cap, "Submission uncertain: %.170s", s->note); goto done;
    }
    if (bn >= 8 && body[4] == 0 && body[5] == 0 && body[6] == 0 && body[7] == 1 && body[2] >= 4) {
        /* an IPP error status is a plain "no": the job was not created */
        unsigned code = (unsigned)body[2] << 8 | body[3];
        attempted = 0;
        if (code == 0x0507)
            snprintf(status, cap, "Not printed: the printer is busy with another job. Try again in a moment.");
        else
            snprintf(status, cap, "Not printed: the printer refused the job (IPP status 0x%04x)", code);
        goto done;
    }
    if (!oap_ipp_parse_caps(body, bn, 1, &s->reply) || !s->reply.job_id) {
        snprintf(status, cap, "Submission uncertain: no successful IPP job-id; check printer"); goto done;
    }
    snprintf(status, cap, "Accepted job %lu by %.80s%s; not yet confirmed printed",
             (unsigned long)s->reply.job_id, s->uri.host, s->uri.secure ? " over IPPS" : "");
    result = OAP_SEND_ACCEPTED;
    goto done;
cancelled:
    result = attempted ? OAP_SEND_UNCERTAIN : OAP_SEND_CANCELLED;
    snprintf(status, cap, attempted ? "Upload stopped; printer outcome uncertain. Check printer before retrying."
                                   : "Cancelled before sending a print job");
done:
    if (attempted && result == OAP_SEND_ERROR) result = OAP_SEND_UNCERTAIN;
    memset(s->authorization, 0, sizeof(s->authorization));
    oap_conn_close(conn);
    oap_net_stop();
    free(body);
    if (f) fclose(f);
    free(s);
    return result;
}
