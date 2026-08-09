/**
 * @file platform_time.h
 * @brief 基于 STM32 HAL Tick 的毫秒时间辅助函数。
 */
#ifndef PLATFORM_TIME_H
#define PLATFORM_TIME_H

#include <stdint.h>

/**
 * @brief 返回当前单调递增 Tick 计数。
 *
 * @return 以可回绕 32 位计数器表示的毫秒时间。
 */
uint32_t Platform_TimeNowMs(void);

/**
 * @brief 检查自保存 Tick 值起指定 Duration 是否已经过去。
 *
 * @param[in] start_ms 在时间间隔开始时保存的 Tick 值。
 * @param[in] duration_ms 要求经过的毫秒 Duration。
 *
 * @return Duration 已经过期时返回非零，包括跨 Tick 回绕的情况。
 */
int Platform_TimeElapsed(uint32_t start_ms, uint32_t duration_ms);

#endif
