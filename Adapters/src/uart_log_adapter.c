/**
 * @file uart_log_adapter.c
 * @brief 基于板级调试串口实现日志输出接口。
 */
#include "adapters/uart_log_adapter.h"

#include <stddef.h>

#include "bsp/bsp_debug.h"

/** 将日志字节流转发到 BSP 调试串口。 */
static firmware_status_t LogWrite(
    void *context,
    const uint8_t *data,
    size_t size)
{
    (void)context;
    return BSP_DebugWrite(data, size);
}

void UartLogAdapter_Init(uart_log_adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return;
    }

    /* 调试串口由 BSP 管理，日志适配器只需绑定写回调。 */
    adapter->interface.context = NULL;
    adapter->interface.write = LogWrite;
}

const log_sink_t *UartLogAdapter_Interface(const uart_log_adapter_t *adapter)
{
    /* 返回适配器内嵌的日志输出接口。 */
    return (adapter == NULL) ? NULL : &adapter->interface;
}
