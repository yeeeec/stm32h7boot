#include "platform/platform_log.h"

#include <stddef.h>

#include "logging.h"
#include "bsp/bsp_log_uart.h"
#include "platform/platform_system.h"

static log_output_port_t s_logging_port;

/** 通过 BSP 调试输出写入 UART。 */
static int UartWrite(void *context, const uint8_t *data, size_t size)
{
    (void) context;
    return (BspLogUart_Write(data, size) == FIRMWARE_STATUS_OK) ? 0 : 1;
}

/** 日志端口使用的时钟回调。 */
static uint32_t LogNowMs(void *context)
{
    (void) context;
    return PlatformSystem_GetMs();
}

firmware_status_t Platform_LogInit(void)
{
    s_logging_port.context = NULL;
    s_logging_port.write = UartWrite;
    s_logging_port.now_ms = LogNowMs;
    if (Logging_SetOutputPort(&s_logging_port) != 0)
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }
    return FIRMWARE_STATUS_OK;
}
