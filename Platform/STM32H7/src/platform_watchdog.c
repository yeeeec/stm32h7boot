/**
 * @file platform_watchdog.c
 * @brief STM32H7 independent-watchdog service implementation.
 */
#include "platform/platform_watchdog.h"

#include "iwdg.h"

firmware_status_t Platform_WatchdogRefresh(void)
{
    if (HAL_IWDG_Refresh(&hiwdg1) != HAL_OK)
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }

    return FIRMWARE_STATUS_OK;
}
