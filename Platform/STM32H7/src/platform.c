#include "platform/platform.h"

#include "platform/platform_log.h"

firmware_status_t Platform_Init(void)
{
    return Platform_LogInit();
}
