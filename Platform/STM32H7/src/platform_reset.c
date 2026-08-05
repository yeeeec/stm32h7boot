#include "platform/platform_reset.h"

#include "stm32h7xx.h"

void Platform_Reset(void)
{
    __disable_irq();
    __DSB();
    NVIC_SystemReset();

    for (;;)
    {
    }
}
