/** platform/include/platform/timer.h */
#ifndef PLATFORM_TIMER_H
#define PLATFORM_TIMER_H

#include <stdint.h>

#include "definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

/** ---------------- Callback Types ---------------- */

#ifdef __cplusplus
}
#endif

#endif /**< PLATFORM_TIMER_H */

typedef void (*Plat_Timer_Callback_t)(void);

/**
 * @brief Init base.
 *
 * @param period_ms
 * @return
 */
Plat_Status_t platform_timer_base_init(uint32_t period_ms);

/**
 * @brief Set callback.
 *
 * @param cb
 */
void platform_timer_set_callback(Plat_Timer_Callback_t cb);

/**
 * @brief Start base.
 *
 */
void platform_timer_start(void);

/**
 * @brief Stop base.
 *
 */
void platform_timer_stop(void);