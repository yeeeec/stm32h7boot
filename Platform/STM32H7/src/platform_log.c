/**
 * @file platform_log.c
 * @brief 日志输出 Platform 适配，支持 UART 和 SEGGER RTT 两种端口。
 */

#include "platform/platform_ports.h"

#include <limits.h>
#include <stddef.h>

#include "SEGGER_RTT.h"
#include "bsp/bsp_debug.h"
#include "platform/platform_time.h"

/* RTT 控制块是否已初始化，避免重复调用 SEGGER_RTT_Init。 */
static uint8_t s_rtt_initialized;

/** 通过 BSP 调试输出写入 UART。 */
static firmware_status_t UartWrite(void *context, const uint8_t *data, size_t size)
{
    (void) context;
    return BSP_DebugWrite(data, size);
}

/** 分块写入 SEGGER RTT，缓冲区满时返回 BUSY。 */
static firmware_status_t RttWrite(void *context, const uint8_t *data, size_t size)
{
    size_t offset = 0U;

    (void) context;
    if ((data == NULL) && (size != 0U))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;

    while (offset < size)
    {
        const size_t remaining     = size - offset;
        const unsigned int request = remaining > UINT_MAX ? UINT_MAX : (unsigned int) remaining;
        const unsigned int chunk   = SEGGER_RTT_Write(0U, &data[offset], request);
        if (chunk == 0U)
            return FIRMWARE_STATUS_BUSY;
        offset += chunk;
    }
    return FIRMWARE_STATUS_OK;
}

/** 日志端口使用的时钟回调。 */
static uint32_t LogNowMs(void *context)
{
    (void) context;
    return Platform_TimeNowMs();
}

/** 按日志输出类型导出 Platform 能力端口。 */
firmware_status_t Platform_GetLogPort(log_output_kind_t kind, log_output_port_t *port)
{
    if (port == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;

    if (kind == LOG_OUTPUT_UART)
    {
        *port = (log_output_port_t) {
            .context = NULL,
            .write   = UartWrite,
            .now_ms  = LogNowMs,
        };
        return FIRMWARE_STATUS_OK;
    }

    if (kind == LOG_OUTPUT_RTT)
    {
        if (s_rtt_initialized == 0U)
        {
            SEGGER_RTT_Init();
            s_rtt_initialized = 1U;
        }
        *port = (log_output_port_t) {
            .context = NULL,
            .write   = RttWrite,
            .now_ms  = LogNowMs,
        };
        return FIRMWARE_STATUS_OK;
    }

    return FIRMWARE_STATUS_INVALID_ARGUMENT;
}
