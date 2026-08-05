#include "adapters/uart_log_adapter.h"

#include <stddef.h>

#include "bsp/bsp_debug.h"

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

    adapter->interface.context = NULL;
    adapter->interface.write = LogWrite;
}

const log_sink_t *UartLogAdapter_Interface(const uart_log_adapter_t *adapter)
{
    return (adapter == NULL) ? NULL : &adapter->interface;
}
