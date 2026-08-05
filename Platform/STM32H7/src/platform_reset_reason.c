/**
 * @file platform_reset_reason.c
 * @brief STM32 RCC reset-flag decoding implementation.
 */
#include "platform/platform_reset_reason.h"

#include "stm32h7xx_hal.h"

static platform_reset_reason_t captured_reason = PLATFORM_RESET_REASON_UNKNOWN;

void Platform_ResetReasonCapture(void)
{
    /* Check causes in priority order before clearing the one-shot RCC flags. */
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
