/**
 * @file platform_reset.h
 * @brief STM32H7 系统 Reset 入口。
 */
#ifndef PLATFORM_RESET_H
#define PLATFORM_RESET_H

#if defined(__GNUC__)
#define PLATFORM_NORETURN __attribute__((noreturn))
#else
#define PLATFORM_NORETURN
#endif

/**
 * @brief 请求系统 Reset 且永不返回。
 *
 * Reset 请求前完成未结束的显式写入。禁用可屏蔽中断，避免 Handler 观察到
 * 部分 Reset 状态。
 *
 * @post 即使硬件延迟执行 Reset 请求，控制权也不会返回。
 */
PLATFORM_NORETURN void Platform_Reset(void);

#endif
