#ifndef OAP_DEVICE_H
#define OAP_DEVICE_H
#include <exec/types.h>
#define OAPCMD_GETSTATS 0x8000
struct OAPDeviceStats {
    ULONG open_calls;
    ULONG close_calls;
    ULONG query_calls;
    ULONG write_calls;
    ULONG last_open_unit;
    ULONG last_open_flags;
};
#endif
