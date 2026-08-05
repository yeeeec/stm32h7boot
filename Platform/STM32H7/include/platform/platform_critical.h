/**
 * @file platform_critical.h
 * @brief Interrupt-mask based critical-section helpers.
 */
#ifndef PLATFORM_CRITICAL_H
#define PLATFORM_CRITICAL_H

#include <stdint.h>

/** Previous PRIMASK value returned by Platform_CriticalEnter. */
typedef uint32_t platform_critical_state_t;

/**
 * @brief Disable maskable interrupts and return the previous interrupt state.
 *
 * @return PRIMASK state to pass unchanged to Platform_CriticalExit.
 *
 * @note The operation is non-blocking and may be used from interrupt-aware code.
 */
platform_critical_state_t Platform_CriticalEnter(void);

/**
 * @brief Restore the interrupt state returned by Platform_CriticalEnter.
 *
 * @param[in] state Value returned by the matching enter call.
 *
 * @pre Calls must be properly nested and each enter call must have one exit call.
 */
void Platform_CriticalExit(platform_critical_state_t state);

#endif
