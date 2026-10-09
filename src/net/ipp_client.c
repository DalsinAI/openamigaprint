/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* IPP over HTTP on an OAPConn: the POST, the bounded response, and
 * Get-Printer-Attributes (include/oap_net.h). */
#include "oap_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int oap_http_post(OAPConn *c, const OAPUri *uri, unsigned long length)
{
    char header[1024];
    int n = snprintf(header, sizeof(header),
                     "POST %s HTTP/1.1\r\nHost: %s:%u\r\nContent-Type: application/ipp\r\n"
                     "Content-Length: %lu\r\nConnection: close\r\nUser-Agent: OpenPrint/0.3\r\n\r\n",
                     uri->path, uri->host, (unsigned)uri->port, length);
    return n > 0 && n < (int)sizeof(header) && oap_conn_write(c, header, (size_t)n, 5);
}

int oap_receive_ipp(OAPConn *c, unsigned char *body, size_t cap, size_t *body_len,
                    unsigned seconds, char *note, size_t note_cap)
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
    free(raw);
    return ok;
}

static const char *state_words(const OAPCaps *caps)
{
    if (caps->accepting == 0)
        return "not accepting jobs";
    return caps->state == 5 ? "stopped" : caps->state == 4 ? "processing" : "ready";
}

int oap_query_pdf(const char *uri_text, OAPCaps *caps, char *note, size_t note_cap)
{
    OAPUri uri;
    OAPConn *c = NULL;
    unsigned char request[2048], *body = NULL;
    size_t request_len, body_len;
    const uint32_t id = 0x4f415001U;
    int ok = 0;
    memset(caps, 0, sizeof(*caps));
    caps->accepting = -1;
    note[0] = 0;
    if (!strncmp(uri_text, "ipps://", 7)) {
        snprintf(note, note_cap, "Secure endpoint discovered; TLS query not available in this browser build");
        return 0;
    }
    if (!oap_parse_ipp_uri(uri_text, &uri)) {
        snprintf(note, note_cap, "Not a printer address OpenPrint can use (ipp://host:port/path)");
        return 0;
    }
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
    c = oap_conn_open(&uri, note, note_cap);
    if (!c)
        goto out;
    if (!oap_http_post(c, &uri, request_len) || !oap_conn_write(c, request, request_len, 5)) {
        snprintf(note, note_cap, "Capability query send failed");
        goto out;
    }
    if (!oap_receive_ipp(c, body, OAP_BODY_MAX, &body_len, 8, note, note_cap))
        goto out;
    if (!oap_ipp_parse_caps(body, body_len, id, caps)) {
        snprintf(note, note_cap, "IPP query rejected or malformed; PDF not verified");
        goto out;
    }
    ok = 1;
    if (caps->pdf == OAP_PDF_YES)
        snprintf(note, note_cap, "PDF confirmed; %s%s%s", state_words(caps), caps->reasons[0] ? "; " : "", caps->reasons);
    else if (caps->pdf == OAP_PDF_NO)
        snprintf(note, note_cap, "Printer does not advertise application/pdf");
    else
        snprintf(note, note_cap, "Printer omitted document-format-supported; not verified");
out:
    oap_conn_close(c);
    free(body);
    return ok;
}
