/**
 * @file platform_watchdog.c
 * @brief Production Independent Watchdog Refresh 边界。
 *
 * Platform 生命周期负责 Refresh Cadence；本模块只转换生成 HAL Handle 的结果。
 * DEBUG 会有意编译掉硬件调用，因此 Debug 执行不能证明 Production Watchdog
 * 已被 Service。
 */
#include "platform/platform_watchdog.h"

#include "iwdg.h"

firmware_status_t Platform_WatchdogRefresh(void)
{
#if defined(DEBUG)
    return FIRMWARE_STATUS_OK;
#else
    if (HAL_IWDG_Refresh(&hiwdg1) != HAL_OK)
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }

    return FIRMWARE_STATUS_OK;
#endif
}
