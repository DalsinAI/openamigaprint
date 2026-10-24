#ifndef OAP_DEVICE_H
#define OAP_DEVICE_H
#include <exec/types.h>
#define OAPCMD_GETSTATS 0x8000
/* oapspool.device to the spooler when it is closed: end the job now, whatever its bytes (24 October 2026). The
 * message is an IOStdReq the device allocated (MEMF_PUBLIC, sizeof(struct IOStdReq)), sent with no reply port;
 * the spooler frees it. */
#define OAPCMD_ENDJOB 0x8001
struct OAPDeviceStats {
    ULONG open_calls;
    ULONG close_calls;
    ULONG query_calls;
    ULONG write_calls;
    ULONG last_open_unit;
    ULONG last_open_flags;
};
#endif
