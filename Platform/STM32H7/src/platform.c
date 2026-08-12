/**
 * @file platform.c
 * @brief Reset Cause 捕获和 Watchdog Maintenance 生命周期。
 *
 * 只有保存 Reset Flag 且 Watchdog Refresh Contract 成功后才发布 Platform 就绪
 * 状态。上层 Event Loop 必须定期调用 Platform_Process()；Application 停滞时，
 * 不会有中断在后台刷新 Watchdog。
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
    platform_initialized     = 1;
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
    /* 无符号减法保证调度比较可跨 Tick 回绕。 */
    if ((uint32_t) (now_ms - last_watchdog_refresh_ms) >= PLATFORM_WATCHDOG_REFRESH_INTERVAL_MS)
    {
        firmware_status_t status = Platform_WatchdogRefresh();

        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }
        /* 只有硬件接受 Refresh 后才推进调度时间点。 */
        last_watchdog_refresh_ms = now_ms;
    }

    return FIRMWARE_STATUS_OK;
}

int Platform_IsInitialized(void)
{
    return platform_initialized;
}
