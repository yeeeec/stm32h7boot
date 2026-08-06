/**
 * @file platform.c
 * @brief STM32H7 platform lifecycle implementation.
 */
#include "platform/platform.h"

#include "platform/platform_reset_reason.h"
#include "platform/platform_time.h"
#include "platform/platform_watchdog.h"

#define PLATFORM_WATCHDOG_REFRESH_INTERVAL_MS 100U

static int platform_initialized;
static uint32_t last_watchdog_refresh_ms;

firmware_status_t Platform_Init(void)
{
    if (platform_initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    Platform_ResetReasonCapture();
    if (!FirmwareStatus_IsOk(Platform_WatchdogRefresh()))
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }
    last_watchdog_refresh_ms = Platform_TimeNowMs();
    platform_initialized = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Platform_Process(void)
{
    uint32_t now_ms;

    if (platform_initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    now_ms = Platform_TimeNowMs();
    /* Unsigned subtraction keeps scheduling valid across tick wraparound. */
    if ((uint32_t)(now_ms - last_watchdog_refresh_ms) >=
        PLATFORM_WATCHDOG_REFRESH_INTERVAL_MS)
    {
        firmware_status_t status = Platform_WatchdogRefresh();

        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }
        last_watchdog_refresh_ms = now_ms;
    }

    return FIRMWARE_STATUS_OK;
}

int Platform_IsInitialized(void)
{
    return platform_initialized;
}
