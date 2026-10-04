/* Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT */
#ifndef OAP_PRINTERS_H
#define OAP_PRINTERS_H
/* The printers OpenAmigaPrint knows, shared by the Print requester and the
 * Printers and Queue window. Each is kept by name with what was verified
 * about it; the address is a detail, not how people choose a printer.
 * Stored as one line per printer in ENV:/ENVARC:OpenAmigaPrint/Printers:
 *   uri <TAB> pdf <TAB> name <TAB> note
 * "Save as PDF file" is always the first entry and is never stored. */
#include <stddef.h>
#include "oap_selection.h"

#define OAP_PRINTERS_MAX 24
#define OAP_PRINTERS_VAR "OpenAmigaPrint/Printers"
#define OAP_PRINTER_NAME_MAX 96
#define OAP_PRINTER_NOTE_MAX 160
#define OAP_SAVE_AS_PDF "file:"

struct OAPDiscovery;

typedef struct OAPPrinter {
    char uri[OAP_SELECTION_URI_MAX];
    char name[OAP_PRINTER_NAME_MAX];
    char note[OAP_PRINTER_NOTE_MAX];
    int pdf;                                   /* OAP_PDF_UNKNOWN / _YES / _NO */
} OAPPrinter;

typedef struct OAPPrinterList {
    OAPPrinter printer[OAP_PRINTERS_MAX];
    int count;
} OAPPrinterList;

/* Save as PDF file first, then the stored printers, then the default printer
 * if it is not among them. Never fails: a missing file is an empty list. */
void oap_printers_load(OAPPrinterList *list);
/* The stored printers (not Save as PDF) to ENV: and ENVARC:. */
int oap_printers_save(const OAPPrinterList *list);
/* Adds or updates the printers a scan found. Returns how many it holds. */
int oap_printers_merge(OAPPrinterList *list, const struct OAPDiscovery *scan);
/* Adds or updates one printer by hand. */
void oap_printers_put(OAPPrinterList *list, const char *uri, const char *name, int pdf, const char *note);
int oap_printers_find(const OAPPrinterList *list, const char *uri);
/* A readable name for an address with no advertised name: its host. */
void oap_printer_name_from_uri(const char *uri, char *name, size_t cap);
int oap_printer_is_file(const OAPPrinter *p);

/* A helper program beside this one (PROGDIR:, as a full path another
 * process can use) or else in C:. */
void oap_program_path(const char *program, char *path, size_t cap);
#endif
