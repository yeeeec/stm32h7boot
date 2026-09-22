#include "platform/platform_system.h"

#include "main.h"

#include "platform/platform_config.h"

#if PLATFORM_WATCHDOG_ENABLE
#include "iwdg.h"
#endif

#if PLATFORM_WATCHDOG_ENABLE
static uint8_t s_watchdog_initialized;
#else
#endif

uint32_t PlatformSystem_GetMs(void)
{
    return HAL_GetTick();
}

void PlatformSystem_DelayMs(uint32_t delay_ms)
{
    HAL_Delay(delay_ms);
}

firmware_status_t PlatformSystem_WatchdogInit(void)
{
#if PLATFORM_WATCHDOG_ENABLE
    if (s_watchdog_initialized == 0U)
    {
        MX_IWDG1_Init();
        s_watchdog_initialized = 1U;
    }
    return FIRMWARE_STATUS_OK;
#else
    return FIRMWARE_STATUS_OK;
#endif
}

void PlatformSystem_WatchdogRefresh(void)
{
#if PLATFORM_WATCHDOG_ENABLE
    if (s_watchdog_initialized != 0U)
    {
        (void) HAL_IWDG_Refresh(&hiwdg1);
    }
#endif
}

void PlatformSystem_Reset(void)
{
    __disable_irq();
    __DSB();
    NVIC_SystemReset();
    for (;;)
    {
    }
}

uint32_t PlatformSystem_GetResetCause(void)
{
    uint32_t cause = 0U;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != 0U)
    {
        cause |= PLATFORM_RESET_CAUSE_SOFTWARE;
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDG1RST) != 0U)
    {
        cause |= PLATFORM_RESET_CAUSE_WATCHDOG;
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDG1RST) != 0U)
    {
        cause |= PLATFORM_RESET_CAUSE_WATCHDOG;
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != 0U)
    {
        cause |= PLATFORM_RESET_CAUSE_POWER_ON;
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST) != 0U)
    {
        cause |= PLATFORM_RESET_CAUSE_BROWN_OUT;
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != 0U)
    {
        cause |= PLATFORM_RESET_CAUSE_PIN;
    }
    return cause;
}

void PlatformSystem_ClearResetCause(void)
{
    __HAL_RCC_CLEAR_RESET_FLAGS();
}
