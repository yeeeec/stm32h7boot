/**
 * @file platform_critical.h
 * @brief 基于 PRIMASK 实现的可嵌套本地 Core Critical Section。
 *
 * 这些函数只在当前 Core 上串行化可屏蔽中断。它们不是 Scheduler Lock、
 * Inter-core Lock 或外设 Ownership 原语。
 */
#ifndef PLATFORM_CRITICAL_H
#define PLATFORM_CRITICAL_H

#include <stdint.h>

/** 由匹配的 Exit 调用恢复的不透明中断屏蔽状态。 */
typedef uint32_t platform_critical_state_t;

/**
 * @brief 禁用可屏蔽中断并返回之前的中断状态。
 *
 * @return 应原样传给 Platform_CriticalExit 的 PRIMASK 状态。
 *
 * @note 操作不会阻塞，可用于感知中断的代码。
 */
platform_critical_state_t Platform_CriticalEnter(void);

/**
 * @brief 恢复 Platform_CriticalEnter 返回的中断状态。
 *
 * @param[in] state 匹配 Enter 调用返回的值。
 *
 * @pre 调用必须按 LIFO 顺序嵌套，并且同一执行上下文中的每次 Enter 都必须
 *      对应一次 Exit。
 */
void Platform_CriticalExit(platform_critical_state_t state);

#endif
