/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* IPP over HTTP on an OAPConn: the POST, the bounded response, and
 * Get-Printer-Attributes (include/oap_net.h). */
#include "oap_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int oap_http_post(OAPConn *c, const OAPUri *uri, unsigned long length, const char *authorization)
{
    char header[2048];
    int n;
    if (authorization && !oap_conn_trusted(c))
        return 0;                              /* never a password in the clear or to an unknown printer */
    n = snprintf(header, sizeof(header),
                 "POST %s HTTP/1.1\r\nHost: %s:%u\r\nContent-Type: application/ipp\r\n"
                 "Content-Length: %lu\r\n%sConnection: close\r\nUser-Agent: OpenPrint/0.4\r\n\r\n",
                 uri->path, uri->host, (unsigned)uri->port, length, authorization ? authorization : "");
    return n > 0 && n < (int)sizeof(header) && oap_conn_write(c, header, (size_t)n, 5);
}

int oap_receive_ipp(OAPConn *c, unsigned char *body, size_t cap, size_t *body_len,
                    unsigned seconds, OAPHttpReply *reply, char *note, size_t note_cap)
{
    unsigned char *raw = malloc(OAP_HTTP_MAX);
    size_t n = 0;
    int http = 0, ok = 0, eof = 0;
    time_t end = time(NULL) + seconds;
    if (!raw) {
        snprintf(note, note_cap, "Not enough memory for the printer's answer");
        return 0;
    }
    note[0] = 0;
    if (reply)
        memset(reply, 0, sizeof(*reply));
    while (time(NULL) <= end) {
        int r;
        long got = oap_conn_read(c, raw + n, OAP_HTTP_MAX - n, 1000);
        if (got == OAP_CONN_TIMEOUT)
            continue;
        if (got < 0) {
            snprintf(note, note_cap, "IPP response interrupted");
            break;
        }
        if (!got)
            eof = 1;
        else
            n += (size_t)got;
        r = oap_http_response(raw, n, eof, body, cap, body_len, &http);
        if (r == 1) {
            ok = 1;
            break;
        }
        if (r < 0) {
            if (http == 401) {
                if (reply)
                    oap_http_header(raw, n, "www-authenticate", reply->challenge, sizeof(reply->challenge));
                snprintf(note, note_cap, "The printer asks for a user name and password");
            } else
                snprintf(note, note_cap, "Invalid/incomplete IPP HTTP response (HTTP %d)", http);
            break;
        }
        if (eof || n == OAP_HTTP_MAX) {
            snprintf(note, note_cap, "Truncated or oversized IPP response");
            break;
        }
    }
    if (!ok && !note[0])
        snprintf(note, note_cap, "IPP response timed out");
    if (reply)
        reply->status = http;
    free(raw);
    return ok;
}

/* What to say when a printer asks for a password, and the Authorization
 * line when one may be sent: only on ipps:// with an accepted certificate,
 * and only with a login kept for that printer. */
int oap_ipp_login(OAPConn *c, const OAPUri *uri, const OAPHttpReply *reply, unsigned counter,
                  char *authorization, size_t cap, char *note, size_t note_cap)
{
    char key[300], user[128], password[128];
    int ok;
    if (!uri->secure) {
        snprintf(note, note_cap, "The printer asks for a password: OpenPrint sends passwords only over ipps://. "
                 "Use the printer's ipps:// address.");
        return 0;
    }
    if (!oap_conn_trusted(c)) {
        snprintf(note, note_cap, "The printer asks for a password, but its certificate isn't trusted yet");
        return 0;
    }
    oap_trust_key(uri->host, uri->port, key, sizeof(key));
    if (!oap_login_lookup(key, user, sizeof(user), password, sizeof(password))) {
        snprintf(note, note_cap, "The printer asks for a user name and password: add them for %.120s "
                 "to ENVARC:OpenPrint/Logins", key);
        return 0;
    }
    ok = oap_http_authorization(reply->challenge, user, password, "POST", uri->path, counter, authorization, cap);
    memset(password, 0, sizeof(password));
    if (!ok)
        snprintf(note, note_cap, "The printer asks for a kind of login OpenPrint can't give (%.60s)", reply->challenge);
    return ok;
}

static const char *state_words(const OAPCaps *caps)
{
    if (caps->accepting == 0)
        return "not accepting jobs";
    return caps->state == 5 ? "stopped" : caps->state == 4 ? "processing" : "ready";
}

/* One Get-Printer-Attributes exchange; 1 when it answered. */
static int query_once(const OAPUri *uri, const char *uri_text, OAPCaps *caps, OAPTlsInfo *tls,
                      unsigned counter, OAPHttpReply *reply, char *note, size_t note_cap)
{
    unsigned char request[2048], *body;
    size_t request_len, body_len;
    const uint32_t id = 0x4f415001U;
    char authorization[1024];
    OAPConn *c;
    int ok = 0, use_auth = reply->status == 401;
    request_len = oap_ipp_query_request(uri_text, id, request, sizeof(request));
    if (!request_len) {
        snprintf(note, note_cap, "Printer address too long");
        return 0;
    }
    body = malloc(OAP_BODY_MAX);
    if (!body) {
        snprintf(note, note_cap, "Not enough memory");
        return 0;
    }
    c = oap_conn_open(uri, OAP_TLS_LOOK, tls, note, note_cap);
    if (!c)
        goto out;
    if (use_auth && !oap_ipp_login(c, uri, reply, counter, authorization, sizeof(authorization), note, note_cap))
        goto out;
    if (!oap_http_post(c, uri, request_len, use_auth ? authorization : NULL) ||
        !oap_conn_write(c, request, request_len, 5)) {
        snprintf(note, note_cap, "Capability query send failed");
        goto out;
    }
    if (!oap_receive_ipp(c, body, OAP_BODY_MAX, &body_len, 8, reply, note, note_cap))
        goto out;
    if (!oap_ipp_parse_caps(body, body_len, id, caps)) {
        snprintf(note, note_cap, "IPP query rejected or malformed; PDF not verified");
        goto out;
    }
    ok = 1;
out:
    memset(authorization, 0, sizeof(authorization));
    oap_conn_close(c);
    free(body);
    return ok;
}

int oap_query_pdf(const char *uri_text, OAPCaps *caps, OAPTlsInfo *tls, char *note, size_t note_cap)
{
    OAPUri uri;
    OAPTlsInfo scratch;
    OAPHttpReply reply;
    int ok;
    if (!tls)
        tls = &scratch;
    memset(tls, 0, sizeof(*tls));
    memset(caps, 0, sizeof(*caps));
    memset(&reply, 0, sizeof(reply));
    caps->accepting = -1;
    note[0] = 0;
    if (!oap_parse_ipp_uri(uri_text, &uri)) {
        snprintf(note, note_cap, "Not a printer address OpenPrint can use (ipp:// or ipps://host:port/path)");
        return 0;
    }
    ok = query_once(&uri, uri_text, caps, tls, 1, &reply, note, note_cap);
    if (!ok && reply.status == 401)            /* once more, with the login */
        ok = query_once(&uri, uri_text, caps, tls, 1, &reply, note, note_cap);
    if (!ok)
        return 0;
    if (caps->pdf == OAP_PDF_YES)
        snprintf(note, note_cap, "PDF confirmed; %s%s%s", state_words(caps), caps->reasons[0] ? "; " : "", caps->reasons);
    else if (caps->pdf == OAP_PDF_NO)
        snprintf(note, note_cap, "Printer does not advertise application/pdf");
    else
        snprintf(note, note_cap, "Printer omitted document-format-supported; not verified");
    if (uri.secure && !oap_cert_usable(tls->verdict)) {
        size_t used = strlen(note);
        snprintf(note + used, note_cap - used, ". %s", oap_cert_words(tls->verdict));
    }
    return 1;
}
