#include "platform/platform_time.h"

#include "stm32h7xx_hal.h"

uint32_t Platform_TimeNowMs(void)
{
    return HAL_GetTick();
}

int Platform_TimeElapsed(uint32_t start_ms, uint32_t duration_ms)
{
    return (uint32_t)(Platform_TimeNowMs() - start_ms) >= duration_ms;
}
