/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
/* The printers OpenPrint knows (include/oap_printers.h). */
#include "oap_printers.h"
#include "oap_discovery.h"
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
#include "oap_str.h"

#define copy oap_copy

int oap_printer_is_file(const OAPPrinter *p)
{
    return p && !strcmp(p->uri, OAP_SAVE_AS_PDF);
}

void oap_printer_name_from_uri(const char *uri, char *name, size_t cap)
{
    const char *host = strstr(uri, "://");
    size_t n;
    host = host ? host + 3 : uri;
    n = strcspn(host, ":/");
    if (n >= cap)
        n = cap - 1;
    memcpy(name, host, n);
    name[n] = 0;
    if (!name[0])
        copy(name, cap, uri);
}

int oap_printers_find(const OAPPrinterList *list, const char *uri)
{
    int i;
    for (i = 0; i < list->count; i++)
        if (!strcmp(list->printer[i].uri, uri))
            return i;
    return -1;
}

void oap_printers_put(OAPPrinterList *list, const char *uri, const char *name, int pdf, const char *note)
{
    int i = oap_printers_find(list, uri);
    OAPPrinter *p;
    if (i < 0) {
        if (list->count >= OAP_PRINTERS_MAX || strlen(uri) >= OAP_SELECTION_URI_MAX)
            return;
        i = list->count++;
        memset(&list->printer[i], 0, sizeof(list->printer[i]));
        copy(list->printer[i].uri, sizeof(list->printer[i].uri), uri);
    }
    p = &list->printer[i];
    if (name && name[0])
        copy(p->name, sizeof(p->name), name);
    else if (!p->name[0])
        oap_printer_name_from_uri(uri, p->name, sizeof(p->name));
    if (pdf != OAP_PDF_UNKNOWN || p->pdf == OAP_PDF_UNKNOWN)
        p->pdf = pdf;
    if (note && note[0])
        copy(p->note, sizeof(p->note), note);
}

static char *field(char **cursor)
{
    char *start = *cursor, *tab;
    if (!start)
        return "";
    tab = strchr(start, '\t');
    if (tab) {
        *tab = 0;
        *cursor = tab + 1;
    } else
        *cursor = NULL;
    return start;
}

void oap_printers_load(OAPPrinterList *list)
{
    static char buffer[OAP_PRINTERS_MAX * 900];
    char def[OAP_SELECTION_URI_MAX];
    LONG got;
    char *line, *next;

    memset(list, 0, sizeof(*list));
    oap_printers_put(list, OAP_SAVE_AS_PDF, "Save as PDF file", OAP_PDF_YES, "Saves the document as a PDF on this Amiga");

    got = GetVar((STRPTR)OAP_PRINTERS_VAR, (STRPTR)buffer, sizeof(buffer), GVF_GLOBAL_ONLY | GVF_BINARY_VAR);
    if (got <= 0)                              /* saved before the rename: OpenAmigaPrint/Printers */
        got = GetVar((STRPTR)OAP_PRINTERS_VAR_OLD, (STRPTR)buffer, sizeof(buffer), GVF_GLOBAL_ONLY | GVF_BINARY_VAR);
    if (got > 0) {
        buffer[got < (LONG)sizeof(buffer) ? got : (LONG)sizeof(buffer) - 1] = 0;
        for (line = buffer; line && *line; line = next) {
            char *cursor = line, *uri, *pdf, *name, *note;
            next = strchr(line, '\n');
            if (next)
                *next++ = 0;
            uri = field(&cursor);
            pdf = field(&cursor);
            name = field(&cursor);
            note = field(&cursor);
            if (!strncmp(uri, "ipp://", 6) || !strncmp(uri, "ipps://", 7))
                oap_printers_put(list, uri, name, pdf[0] - '0', note);
        }
    }
    got = GetVar((STRPTR)"OpenPrint/PrinterURI", (STRPTR)def, sizeof(def), GVF_GLOBAL_ONLY);
    if (got <= 0)                              /* saved before the rename */
        got = GetVar((STRPTR)"OpenAmigaPrint/PrinterURI", (STRPTR)def, sizeof(def), GVF_GLOBAL_ONLY);
    if (got > 6 && !strncmp(def, "ipp://", 6) && oap_printers_find(list, def) < 0)
        oap_printers_put(list, def, NULL, OAP_PDF_YES, "Your default printer");
}

int oap_printers_save(const OAPPrinterList *list)
{
    static char buffer[OAP_PRINTERS_MAX * 900];
    size_t used = 0;
    int i;
    for (i = 0; i < list->count; i++) {
        const OAPPrinter *p = &list->printer[i];
        int n;
        if (oap_printer_is_file(p))
            continue;
        n = snprintf(buffer + used, sizeof(buffer) - used, "%s\t%d\t%s\t%s\n", p->uri, p->pdf, p->name, p->note);
        if (n < 0 || (size_t)n >= sizeof(buffer) - used)
            break;
        used += (size_t)n;
    }
    return SetVar((STRPTR)OAP_PRINTERS_VAR, (STRPTR)buffer, (LONG)used, GVF_GLOBAL_ONLY | GVF_SAVE_VAR | GVF_BINARY_VAR) != 0;
}

int oap_printers_merge(OAPPrinterList *list, const OAPDiscovery *scan)
{
    size_t i;
    for (i = 0; i < scan->count; i++) {
        const OAPDiscovered *d = &scan->printers[i];
        const char *name = d->caps.name[0] ? d->caps.name : d->label;
        if (!d->uri[0])
            continue;
        oap_printers_put(list, d->uri, name, d->caps.pdf, d->note);
    }
    return list->count;
}

void oap_program_path(const char *program, char *path, size_t cap)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR requesters;
    BPTR lock = 0;
    if (GetProgramDir()) {                     /* only a program started from a file has PROGDIR: */
        snprintf(path, cap, "PROGDIR:%s", program);
        requesters = me->pr_WindowPtr;
        me->pr_WindowPtr = (APTR)-1;           /* no "insert volume" requester for a quiet check */
        lock = Lock((STRPTR)path, ACCESS_READ);
        me->pr_WindowPtr = requesters;
    }
    if (lock) {
        /* the full name: another process (a new shell) has its own PROGDIR: */
        int named = NameFromLock(lock, (STRPTR)path, (LONG)cap) != 0;
        UnLock(lock);
        if (named)
            return;
    }
    snprintf(path, cap, "C:%s", program);
}
