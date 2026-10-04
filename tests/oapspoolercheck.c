#include <exec/types.h>
#include <exec/ports.h>
#include <proto/exec.h>
#include <stdio.h>
#include "oap_spooler.h"
int main(void)
{
    struct MsgPort *p;
    Forbid();p=FindPort((STRPTR)OAP_SPOOLER_PORT);Permit();
    if(!p){puts("FAIL: OpenPrint spooler port is not present");return 20;}
    puts("PASS: OpenPrint spooler port is present");return 0;
}
