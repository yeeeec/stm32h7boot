/** platform/include/platform/clock.h */
#ifndef PLATFORM_CLOCK_H
#define PLATFORM_CLOCK_H

#include <stdint.h>

#include "definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

/** ---------------- API Functions ---------------- */

/**
 * @brief Init system.
 *
 */
void platform_system_init(void);

/**
 * @brief Init platform.
 *
 */
void platform_clock_init(void);

/**
 * @brief Get systick.
 *
 * @return
 */
uint32_t clock_get_systick(void);

/**
 * @brief Delay ms.
 *
 * @param ms
 */
void clock_delay_ms(uint32_t ms);


#ifdef __cplusplus
}
#endif

#endif /**< PLATFORM_CLOCK_H */