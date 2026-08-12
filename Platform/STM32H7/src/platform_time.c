/**
 * @file platform_time.c
 * @brief 基于 STM32 HAL Tick 的可回绕单调时间。
 *
 * Elapsed Time 比较使用无符号减法，这是跨越 32 位 Tick 回绕时必须保持的
 * 不变量。
 */
#include "platform/platform_time.h"

#include "stm32h7xx_hal.h"

uint32_t Platform_TimeNowMs(void)
{
    return HAL_GetTick();
}

int Platform_TimeElapsed(uint32_t start_ms, uint32_t duration_ms)
{
    return (uint32_t) (Platform_TimeNowMs() - start_ms) >= duration_ms;
}
