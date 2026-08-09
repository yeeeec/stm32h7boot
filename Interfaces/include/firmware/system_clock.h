/**
 * @file system_clock.h
 * @brief 单调递增毫秒时钟 Provider Contract。
 */
#ifndef FIRMWARE_SYSTEM_CLOCK_H
#define FIRMWARE_SYSTEM_CLOCK_H

#include <stdint.h>

/** 返回允许回绕的单调递增毫秒计数器。 */
typedef uint32_t (*system_clock_now_ms_fn)(void *context);

/**
 * @brief Timeout 与 Retry 策略使用的 Clock 接口。
 *
 * 计数器可能回绕；调用者必须用无符号减法比较 elapsed time，不能直接比较
 * 原始时间戳的大小。
 */
typedef struct
{
    void *context;
    system_clock_now_ms_fn now_ms;
} system_clock_t;

#endif
