#include "platform/platform_system.h"

#include "main.h"

#ifndef PLATFORM_WATCHDOG_ENABLE
#define PLATFORM_WATCHDOG_ENABLE 1
#endif

#if PLATFORM_WATCHDOG_ENABLE
#include "iwdg.h"
#endif

static uint8_t s_watchdog_initialized;

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
    return FIRMWARE_STATUS_NOT_SUPPORTED;
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
