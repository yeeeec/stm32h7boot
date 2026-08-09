/**
 * @file platform_reset_reason.c
 * @brief STM32 RCC Reset Flag 的一次性捕获。
 *
 * RCC 可能同时保留多个 Cause Flag。先检查具体的 Watchdog 和 Software Cause，
 * 再检查通用 Power/Pin 标识，使诊断保留最有价值的原因。只有将结果复制到
 * Platform 静态状态后才清除这些 Flag。
 */
#include "platform/platform_reset_reason.h"

#include "stm32h7xx_hal.h"

static platform_reset_reason_t captured_reason = PLATFORM_RESET_REASON_UNKNOWN;

void Platform_ResetReasonCapture(void)
{
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDG1RST) != 0U)
    {
        captured_reason = PLATFORM_RESET_REASON_INDEPENDENT_WATCHDOG;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDG1RST) != 0U)
    {
        captured_reason = PLATFORM_RESET_REASON_WINDOW_WATCHDOG;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != 0U)
    {
        captured_reason = PLATFORM_RESET_REASON_SOFTWARE;
    }
    else if ((__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST) != 0U) ||
             (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != 0U))
    {
        captured_reason = PLATFORM_RESET_REASON_POWER_ON;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWR1RST) != 0U)
    {
        captured_reason = PLATFORM_RESET_REASON_LOW_POWER;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != 0U)
    {
        captured_reason = PLATFORM_RESET_REASON_PIN;
    }
    else
    {
        captured_reason = PLATFORM_RESET_REASON_UNKNOWN;
    }

    __HAL_RCC_CLEAR_RESET_FLAGS();
}

platform_reset_reason_t Platform_ResetReasonGet(void)
{
    return captured_reason;
}
