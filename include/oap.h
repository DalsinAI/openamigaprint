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
#ifdef __amigaos__
int oap_ipp_submit_pdf(const char *pdf_path,const OAPJobOptions *o,char *status,size_t status_len);
int oap_run_print_dialog(const char *pdf_path,OAPJobOptions *o);
int oap_run_queue_window(void);
#endif

#endif
