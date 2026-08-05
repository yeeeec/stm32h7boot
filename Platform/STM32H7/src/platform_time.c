/**
 * @file platform_time.c
 * @brief Millisecond time helpers backed by the STM32 HAL tick.
 */
#include "platform/platform_time.h"

#include "stm32h7xx_hal.h"

uint32_t Platform_TimeNowMs(void)
{
    return HAL_GetTick();
}

int Platform_TimeElapsed(uint32_t start_ms, uint32_t duration_ms)
{
    /* Unsigned subtraction keeps this comparison valid across tick wraparound. */
    return (uint32_t)(Platform_TimeNowMs() - start_ms) >= duration_ms;
}
