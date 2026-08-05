/**
 * @file platform_time.h
 * @brief Millisecond time helpers backed by the STM32 HAL tick.
 */
#ifndef PLATFORM_TIME_H
#define PLATFORM_TIME_H

#include <stdint.h>

/**
 * @brief Return the current monotonic tick count.
 *
 * @return Elapsed time in milliseconds as a wrapping 32-bit counter.
 */
uint32_t Platform_TimeNowMs(void);

/**
 * @brief Check whether a duration has elapsed since a saved tick value.
 *
 * @param[in] start_ms Tick value captured at the start of the interval.
 * @param[in] duration_ms Required duration in milliseconds.
 *
 * @return Nonzero when the duration has elapsed, including across tick wraparound.
 */
int Platform_TimeElapsed(uint32_t start_ms, uint32_t duration_ms);

#endif
