/**
 * @file platform.c
 * @brief Platform 层基础能力：初始化 CAN、提供单调时钟和看门狗端口。
 */

#include "platform/platform.h"
#include "platform/platform_ports.h"

#include <stddef.h>

#include "stm32h7xx_hal.h"

#include "iwdg.h"

/** clock_port_t 的适配回调。 */
static uint32_t ClockNowMs(void *context)
{
    (void) context;
    return HAL_GetTick();
}

/** 返回 HAL 提供的单调毫秒计时值。 */
uint32_t Platform_TimeNowMs(void)
{
    return HAL_GetTick();
}

/** runtime_port_t 的看门狗喂狗回调。 */
static firmware_status_t RuntimeKickWatchdog(void *context)
{
    (void) context;
#if !defined(DEBUG)
    (void) HAL_IWDG_Refresh(&hiwdg1);
#endif
    return FIRMWARE_STATUS_OK;
}


/** 初始化 Platform 必需的通信硬件。 */
firmware_status_t Platform_Init(void)
{
    return Platform_CanStart();
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

/** 导出仅包含单调时钟的能力端口。 */
firmware_status_t Platform_GetClockPort(clock_port_t *port)
{
    if (port == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    *port = (clock_port_t) {.context = NULL, .now_ms = ClockNowMs};
    return FIRMWARE_STATUS_OK;
}
