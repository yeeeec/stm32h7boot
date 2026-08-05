#ifndef STM32_WATCHDOG_ADAPTER_H
#define STM32_WATCHDOG_ADAPTER_H

#include "firmware/watchdog.h"

typedef struct
{
    watchdog_t interface;
} stm32_watchdog_adapter_t;

void STM32WatchdogAdapter_Init(stm32_watchdog_adapter_t *adapter);
const watchdog_t *STM32WatchdogAdapter_Interface(
    const stm32_watchdog_adapter_t *adapter);

#endif
