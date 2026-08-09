/**
 * @file platform_watchdog.h
 * @brief STM32H7 Independent Watchdog Service。
 */
#ifndef PLATFORM_WATCHDOG_H
#define PLATFORM_WATCHDOG_H

#include "firmware/status.h"

/**
 * @brief 刷新已配置的 Independent Watchdog。
 *
 * @return HAL 接受 Refresh 时返回 FIRMWARE_STATUS_OK。
 * @return HAL Refresh 失败时返回 FIRMWARE_STATUS_IO_ERROR。
 *
 * @note DEBUG 构建会去掉 HAL Refresh 并返回成功；该结果不能证明物理
 *       Watchdog 的实际状态。
 */
firmware_status_t Platform_WatchdogRefresh(void);

#endif
