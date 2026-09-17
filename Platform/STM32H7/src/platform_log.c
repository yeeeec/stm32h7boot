#include "platform/platform_log.h"

#include <limits.h>
#include <stddef.h>

#include "logging.h"
#include "SEGGER_RTT.h"
#include "bsp/bsp_log_uart.h"
#include "platform/platform_system.h"

/* RTT 控制块是否已初始化，避免重复调用 SEGGER_RTT_Init。 */
static uint8_t s_rtt_initialized;
static log_output_port_t s_logging_port;

/** 通过 BSP 调试输出写入 UART。 */
static int UartWrite(void *context, const uint8_t *data, size_t size)
{
    (void) context;
    return (BspLogUart_Write(data, size) == FIRMWARE_STATUS_OK) ? 0 : 1;
}

/** 分块写入 SEGGER RTT，缓冲区满时返回 BUSY。 */
static int RttWrite(void *context, const uint8_t *data, size_t size)
{
    size_t offset = 0U;

    (void) context;
    if ((data == NULL) && (size != 0U))
        return 1;

    while (offset < size)
    {
        const size_t remaining     = size - offset;
        const unsigned int request = remaining > UINT_MAX ? UINT_MAX : (unsigned int) remaining;
        const unsigned int chunk   = SEGGER_RTT_Write(0U, &data[offset], request);
        if (chunk == 0U)
            return 2;
        offset += chunk;
    }
    return 0;
}

/** 日志端口使用的时钟回调。 */
static uint32_t LogNowMs(void *context)
{
    (void) context;
    return PlatformSystem_GetMs();
}

firmware_status_t Platform_LogInit(void)
{
    return FIRMWARE_STATUS_OK;
}

/** 按日志输出类型导出 Platform 能力端口。 */
firmware_status_t Platform_SetLogPort(log_output_kind_t kind)
{
    if (kind == LOG_OUTPUT_UART)
    {
        s_logging_port.context = NULL;
        s_logging_port.write   = UartWrite;
        s_logging_port.now_ms  = LogNowMs;
        return FIRMWARE_STATUS_OK;
    }

    if (kind == LOG_OUTPUT_RTT)
    {
        if (s_rtt_initialized == 0U)
        {
            SEGGER_RTT_Init();
            s_rtt_initialized = 1U;
        }
        s_logging_port.context = NULL;
        s_logging_port.write   = RttWrite;
        s_logging_port.now_ms  = LogNowMs;
        return FIRMWARE_STATUS_OK;
    }
    Logging_SetOutputPort(&s_logging_port);

    return FIRMWARE_STATUS_INVALID_ARGUMENT;
}
