/**
 * @file platform_reset_reason.h
 * @brief 仅捕获一次的 Reset Cause 查询 API。
 */
#ifndef PLATFORM_RESET_REASON_H
#define PLATFORM_RESET_REASON_H

/** 根据锁存的 STM32 RCC Flag 集合解码的 Reset Cause。 */
typedef enum
{
    PLATFORM_RESET_REASON_UNKNOWN = 0,
    PLATFORM_RESET_REASON_POWER_ON,
    PLATFORM_RESET_REASON_PIN,
    PLATFORM_RESET_REASON_SOFTWARE,
    PLATFORM_RESET_REASON_INDEPENDENT_WATCHDOG,
    PLATFORM_RESET_REASON_WINDOW_WATCHDOG,
    PLATFORM_RESET_REASON_LOW_POWER
} platform_reset_reason_t;

/**
 * @brief 解码并保存当前 RCC Reset Flag。
 *
 * 当硬件报告多个 Flag 时，Watchdog 和 Software Cause 优先于通用 Power 或
 * Pin Reset 标识。
 *
 * @pre 在 Platform_Init() 期间调用一次，并且要早于其他代码清除 RCC Reset Flag。
 *
 * @note 捕获后会清除 RCC Reset Flag，因此之后只能通过
 *       Platform_ResetReasonGet 获取该信息。
 */
void Platform_ResetReasonCapture(void);

/**
 * @brief 返回最近一次捕获的 Reset Cause。
 *
 * @return 已捕获的 Reset Cause；捕获前返回 PLATFORM_RESET_REASON_UNKNOWN。
 */
platform_reset_reason_t Platform_ResetReasonGet(void);

#endif
