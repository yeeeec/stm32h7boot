#ifndef STM32_CLOCK_ADAPTER_H
#define STM32_CLOCK_ADAPTER_H

#include "firmware/system_clock.h"

typedef struct
{
    system_clock_t interface;
} stm32_clock_adapter_t;

void STM32ClockAdapter_Init(stm32_clock_adapter_t *adapter);
const system_clock_t *STM32ClockAdapter_Interface(
    const stm32_clock_adapter_t *adapter);

#endif
