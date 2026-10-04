/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef OAP_SELECTION_H
#define OAP_SELECTION_H
#include <stddef.h>
/* Each open print/viewer window subscribes independently. Selection is an
 * explicit event, not a comparison against the last saved URI. */
#define OAP_SELECTION_URI_MAX 384
#define OAP_SELECTION_NAME_MAX 64
#define OAP_SELECTION_MAX_LISTENERS 64
#ifndef OAP_SELECTION_PREFIX
#define OAP_SELECTION_PREFIX "OAP.PrinterSelection."
#endif
struct MsgPort;
typedef struct OAPSelectionListener {
    struct MsgPort *port;
    char name[OAP_SELECTION_NAME_MAX];
} OAPSelectionListener;
int oap_selection_open(OAPSelectionListener *);
void oap_selection_close(OAPSelectionListener *);
unsigned long oap_selection_mask(const OAPSelectionListener *);
int oap_selection_receive(OAPSelectionListener *, char *, size_t);
/* Returns recipients notified, or -1 on allocation/validation failure.
 * No file writes, network traffic or print submission occur here. */
int oap_selection_publish(const char *uri);
int oap_preferences_store(const char *name,const char *uri,char *error,size_t cap);
#endif
