/**
 * @file platform.h
 * @brief STM32H7 处理器级生命周期和周期维护。
 *
 * Platform 负责 Reset Cause 捕获和 Watchdog Service。板级外设及外部设备仍由
 * BSP 负责。
 */
#ifndef PLATFORM_H
#define PLATFORM_H

/* Platform owns processor lifecycle primitives (MPU/cache, reset reason,
 * watchdog and tick); board peripherals remain in BSP. */

#include "firmware/status.h"

/**
 * @brief 初始化 Platform 状态并捕获 Reset Reason。
 *
 * 在正常处理前捕获锁存的 RCC Reset flag，且 Watchdog Refresh Contract 必须
 * 成功后才能发布就绪状态。初始化失败是当前 Boot 的终态；Reset flag 被消费后
 * 不得重试。
 *
 * @pre HAL 和生成的 Independent Watchdog Handle 已完成初始化。
 * @pre 当前 Boot 中尚未调用过本函数。
 *
 * @return 首次初始化成功时返回 FIRMWARE_STATUS_OK。
 * @return 成功初始化后再次调用时返回 FIRMWARE_STATUS_INVALID_STATE。
 * @return 首次 Watchdog Refresh 失败时返回 FIRMWARE_STATUS_IO_ERROR。
 */
firmware_status_t Platform_Init(void);

/**
 * @brief 执行周期性的 Platform 维护。
 *
 * 调用有界，并按 Platform Maintenance Interval 刷新 Independent Watchdog。
 * 因此上层停滞会停止刷新 Watchdog。调用者必须将错误视为致命错误，不能绕过
 * Watchdog Policy。
 *
 * @return 维护成功时返回 FIRMWARE_STATUS_OK。
 * @return Platform_Init 前返回 FIRMWARE_STATUS_INVALID_STATE。
 * @return Watchdog Refresh 失败时返回 FIRMWARE_STATUS_IO_ERROR。
 */
firmware_status_t Platform_Process(void);

/**
 * @brief 查询 Platform_Init 是否成功完成。
 *
 * @return 仅在成功初始化后返回非零；初始化前和失败后返回零。
 */
int Platform_IsInitialized(void);

#endif
