/**
 * @file stm32_watchdog_adapter.c
 * @brief Watchdog interface backed by the STM32 platform watchdog.
 */
#include "adapters/stm32_watchdog_adapter.h"

#include <stddef.h>

#include "platform/platform_watchdog.h"

static firmware_status_t WatchdogRefresh(void *context)
{
    (void)context;
    return Platform_WatchdogRefresh();
}

void STM32WatchdogAdapter_Init(stm32_watchdog_adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return;
    }

    adapter->interface.context = NULL;
    adapter->interface.refresh = WatchdogRefresh;
}

const watchdog_t *STM32WatchdogAdapter_Interface(
    const stm32_watchdog_adapter_t *adapter)
{
    return (adapter == NULL) ? NULL : &adapter->interface;
}
