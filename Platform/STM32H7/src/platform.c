#include "platform/platform.h"

#include <stddef.h>

#include "platform/platform_boot_control.h"
#include "platform/platform_log.h"

firmware_status_t Platform_Init(void)
{
    Platform_LogInit();
    Platform_SetLogPort(LOG_OUTPUT_UART);

    return PlatformBootControl_Init();
}
