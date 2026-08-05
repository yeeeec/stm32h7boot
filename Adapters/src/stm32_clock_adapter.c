#include "adapters/stm32_clock_adapter.h"

#include <stddef.h>

#include "platform/platform_time.h"

static uint32_t ClockNowMs(void *context)
{
    (void)context;
    return Platform_TimeNowMs();
}

void STM32ClockAdapter_Init(stm32_clock_adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return;
    }

    adapter->interface.context = NULL;
    adapter->interface.now_ms = ClockNowMs;
}

const system_clock_t *STM32ClockAdapter_Interface(
    const stm32_clock_adapter_t *adapter)
{
    return (adapter == NULL) ? NULL : &adapter->interface;
}
