#include "platform/platform_critical.h"

#include "stm32h7xx.h"

platform_critical_state_t Platform_CriticalEnter(void)
{
    platform_critical_state_t state = __get_PRIMASK();

    __disable_irq();
    __DMB();
    return state;
}

void Platform_CriticalExit(platform_critical_state_t state)
{
    __DMB();
    if ((state & 1U) == 0U)
    {
        __enable_irq();
    }
}
