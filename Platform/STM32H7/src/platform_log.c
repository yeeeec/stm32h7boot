#include "platform/platform_ports.h"

#include <stddef.h>

#include "bsp/bsp_debug.h"
#include "platform/platform_time.h"

static firmware_status_t UartWrite(void *context, const uint8_t *data, size_t size)
{
    (void) context;
    return BSP_DebugWrite(data, size);
}

static uint32_t LogNowMs(void *context)
{
    (void) context;
    return Platform_TimeNowMs();
}

firmware_status_t Platform_GetLogPort(log_output_kind_t kind, log_output_port_t *port)
{
    if (port == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    if (kind != LOG_OUTPUT_UART)
        return FIRMWARE_STATUS_NOT_SUPPORTED;

    *port = (log_output_port_t) {
        .context = NULL,
        .write   = UartWrite,
        .now_ms  = LogNowMs,
    };
    return FIRMWARE_STATUS_OK;
}
