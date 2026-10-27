/* Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT */
/* IPP Print-Job of a PDF, or of a program's own PostScript, to an ipp:// or ipps:// printer (include/oap.h). */
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
        snprintf(status, cap, "%s", "This printer uses IPPS (encrypted IPP), which needs opentls.library. "
                 "Install it, or use the printer's ipp:// address.");
    else if (tls->verdict == OAP_CERT_ASK)
        snprintf(status, cap, "This printer's certificate isn't trusted yet: in Printers and Queue, select it and "
                 "click Use for printing to check and trust it");
    else if (tls->verdict == OAP_CERT_CHANGED)
        snprintf(status, cap, "This printer's certificate has changed since you trusted it: check it in Printers "
                 "and Queue before printing");
    else
        snprintf(status, cap, "Not sent: %s", oap_cert_words(tls->verdict));
}

/* want: the one format the caller sends (OAP_FORMAT_PDF), or NULL for a PDF or PostScript, by its first bytes. */
static int submit(const char *pdf, const char *want, const OAPJobOptions *o,
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
    int result = OAP_SEND_ERROR, attempted = 0, tries, ps;
    const char *format, *kind;
    if (!pdf || !o || !status || !cap) return OAP_SEND_ERROR;
    status[0] = 0;
    s = calloc(1, sizeof(*s));
    if (!s) { snprintf(status, cap, "Cannot allocate print workspace"); return OAP_SEND_ERROR; }
    if (!oap_parse_ipp_uri(o->printer_uri, &s->uri)) {
        snprintf(status, cap, "Invalid ipp:// or ipps:// URI; select a verified printer"); goto done;
    }
    if (!progress(notify, ctx, "preparing", 0, 0)) goto cancelled;
    f = fopen(pdf, "rb");
    if (!f) { snprintf(status, cap, "%s", want ? "Cannot open PDF spool file" : "Cannot open the document to print"); goto done; }
    if (fseek(f, 0, SEEK_END) || (flen = ftell(f)) < 2 || fseek(f, 0, SEEK_SET)) {
        snprintf(status, cap, "Cannot measure %s", want ? "PDF" : "the document"); goto done;
    }
    /* what it is, and a PostScript document's own name, from its first 4 KB */
    n = fread(s->chunk, 1, 4096, f);
    format = oap_document_format(s->chunk, n);
    if (!format || (want && strcmp(format, want)) || fseek(f, 0, SEEK_SET)) {
        snprintf(status, cap, "%s", want ? "Source is not a PDF document"
                 : "Not sent: OpenPrint sends PDF and PostScript to printers, and this is neither");
        goto done;
    }
    ps = !strcmp(format, OAP_FORMAT_PS);
    kind = ps ? "PostScript" : "PDF";
    s->effective = *o;
    if (ps && oap_ps_title(s->chunk, n, s->note, sizeof(s->effective.job_name)))
        memcpy(s->effective.job_name, s->note, sizeof(s->effective.job_name));
    else if (ps && !s->effective.job_name[0]) {            /* no name given: the file's */
        const char *base = pdf + strlen(pdf);
        while (base > pdf && base[-1] != '/' && base[-1] != ':') base--;
        snprintf(s->effective.job_name, sizeof(s->effective.job_name), "%s", base);
    }
    if (!progress(notify, ctx, "checking", 0, (unsigned long)flen)) goto cancelled;
    if (!oap_net_start()) { snprintf(status, cap, "bsdsocket.library is not available"); goto done; }
    if (!oap_query_pdf(o->printer_uri, &s->capabilities, &s->tls, s->note, sizeof(s->note))) {
        if (s->uri.secure && s->tls.verdict == OAP_CERT_NOTLS)
            untrusted(&s->tls, status, cap);
        else
            snprintf(status, cap, "%s not confirmed: %.180s", kind, s->note);
        goto done;
    }
    if (s->uri.secure && !oap_cert_usable(s->tls.verdict)) { untrusted(&s->tls, status, cap); goto done; }
    if (ps && s->capabilities.ps != OAP_PDF_YES) {
        /* asked first, so a printer that can't read PostScript is never sent it (it might print pages of code) */
        snprintf(status, cap, s->capabilities.ps == OAP_PDF_NO
                 ? "Not sent: this printer doesn't take PostScript (application/postscript isn't among its formats)"
                 : "Not sent: the printer didn't say which formats it takes, so PostScript isn't confirmed");
        goto done;
    }
    if (!ps && s->capabilities.pdf != OAP_PDF_YES) { snprintf(status, cap, "PDF not confirmed: %.180s", s->note); goto done; }
    if (s->capabilities.accepting == 0) { snprintf(status, cap, "Printer is not accepting jobs"); goto done; }
    if (!s->capabilities.color) s->effective.color = OAP_MONO;
    if (!ps && s->effective.duplex != OAP_SIMPLEX && !s->capabilities.duplex) {
        snprintf(status, cap, "Printer has not confirmed duplex; choose one-sided"); goto done;
    }
    if (!oap_ipp_build_prefix_doc(&s->effective, format, s->prefix, sizeof(s->prefix), &plen)) {
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
        if (fseek(f, 0, SEEK_SET)) { snprintf(status, cap, "Cannot read the %s again", kind); goto done; }
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
                snprintf(status, cap, "Submission uncertain: %s upload interrupted; check printer", kind); goto done;
            }
            uploaded += (unsigned long)n;
        }
        if (ferror(f) || uploaded != (unsigned long)flen) {
            snprintf(status, cap, "Submission uncertain: %s read length changed", kind); goto done;
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
        else if (code == 0x040a)
            snprintf(status, cap, "Not printed: the printer doesn't take %s (IPP status 0x040a)", kind);
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

int oap_ipp_submit_pdf_ex(const char *pdf, const OAPJobOptions *o, char *status, size_t cap,
                          OAPSendProgress notify, void *ctx)
{
    return submit(pdf, OAP_FORMAT_PDF, o, status, cap, notify, ctx);
}

int oap_ipp_submit_doc_ex(const char *path, const OAPJobOptions *o, char *status, size_t cap,
                          OAPSendProgress notify, void *ctx)
{
    return submit(path, NULL, o, status, cap, notify, ctx);
}
