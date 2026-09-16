#include "platform/platform.h"

#include <stddef.h>

#include "bsp/bsp_log_uart.h"
#include "logging.h"
#include "platform/platform_boot_control.h"
#include "platform/platform_system.h"
#include "ports/log_output.h"

static uint32_t LogNowMs(void *context)
{
    (void) context;
    return PlatformSystem_GetMs();
}

firmware_status_t Platform_Init(void)
{
    const log_output_port_t log_port = {
        .context = NULL, .write = BspLogUart_Write, .now_ms = LogNowMs};
    firmware_status_t status;

    status = Logging_Init(&log_port);
    if ((status != FIRMWARE_STATUS_OK) && (status != FIRMWARE_STATUS_INVALID_STATE))
    {
        return status;
    }

    return PlatformBootControl_Init();
}
