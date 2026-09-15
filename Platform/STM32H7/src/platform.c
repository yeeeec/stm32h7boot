/**
 * @file platform.c
 * @brief Reset Cause 捕获和 Watchdog Maintenance 生命周期。
 *
 * 只有保存 Reset Flag 且 Watchdog Refresh Contract 成功后才发布 Platform 就绪
 * 状态。上层 Event Loop 必须定期调用 Platform_Process()；Application 停滞时，
 * 不会有中断在后台刷新 Watchdog。
 */
#include "platform/platform.h"
#include "platform/platform_ports.h"

#include "platform/platform_reset_reason.h"
#include "platform/platform_time.h"

#include "iwdg.h"
#include "stm32h7xx_hal.h"

#define PLATFORM_WATCHDOG_REFRESH_INTERVAL_MS 100U

static int platform_initialized;
static uint32_t last_watchdog_refresh_ms;

static uint32_t ClockNowMs(void *context)
{
    (void) context;
    return Platform_TimeNowMs();
}

static firmware_status_t RuntimeKickWatchdog(void *context)
{
    (void) context;
#if !defined(DEBUG)
    (void) HAL_IWDG_Refresh(&hiwdg1);
#endif
    return FIRMWARE_STATUS_OK;
}

static void ConfigureMemoryProtectionAndCache(void)
{
    MPU_Region_InitTypeDef region = {0};

    HAL_MPU_Disable();
    region.Enable           = MPU_REGION_ENABLE;
    region.Number           = MPU_REGION_NUMBER0;
    region.BaseAddress      = 0x90000000UL;
    region.Size             = MPU_REGION_SIZE_32MB;
    region.SubRegionDisable = 0U;
    region.TypeExtField     = MPU_TEX_LEVEL0;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
    region.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    region.IsCacheable      = MPU_ACCESS_CACHEABLE;
    region.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&region);
    HAL_MPU_Enable(MPU_HFNMI_PRIVDEF);

    SCB_EnableICache();
    SCB_EnableDCache();
}

firmware_status_t Platform_Init(void)
{
    if (platform_initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    ConfigureMemoryProtectionAndCache();
    MX_IWDG1_Init();
    Platform_ResetReasonCapture();
    HAL_IWDG_Refresh(&hiwdg1);
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
        HAL_IWDG_Refresh(&hiwdg1);
        /* 只有硬件接受 Refresh 后才推进调度时间点。 */
        last_watchdog_refresh_ms = now_ms;
    }

    return FIRMWARE_STATUS_OK;
}

int Platform_IsInitialized(void)
{
    return platform_initialized;
}

/** 导出时钟和看门狗运行时能力端口。 */
firmware_status_t Platform_GetRuntimePort(runtime_port_t *port)
{
    if (port == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    *port = (runtime_port_t) {
        .context       = NULL,
        .now_ms        = ClockNowMs,
        .kick_watchdog = RuntimeKickWatchdog,
    };
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Platform_GetClockPort(clock_port_t *port)
{
    if (port == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    *port = (clock_port_t) {.context = NULL, .now_ms = ClockNowMs};
    return FIRMWARE_STATUS_OK;
}
