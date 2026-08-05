/**
 * @file platform_reset.c
 * @brief STM32H7 system-reset implementation.
 */
#include "platform/platform_reset.h"

#include "stm32h7xx.h"

void Platform_Reset(void)
{
    /* Complete outstanding writes before handing control to the reset sequence. */
    __disable_irq();
    __DSB();
    NVIC_SystemReset();

    for (;;)
    {
    }
}
