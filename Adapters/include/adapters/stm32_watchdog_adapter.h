/**
 * @file stm32_watchdog_adapter.h
 * @brief Adapter from the STM32 platform watchdog to the watchdog interface.
 */
#ifndef STM32_WATCHDOG_ADAPTER_H
#define STM32_WATCHDOG_ADAPTER_H

#include "firmware/watchdog.h"

/** Owns a watchdog interface backed by the configured STM32 IWDG instance. */
typedef struct
{
    watchdog_t interface;
} stm32_watchdog_adapter_t;

/**
 * @brief Initialize an STM32 watchdog adapter.
 *
 * @param[out] adapter Adapter instance to initialize; NULL is ignored.
 */
void STM32WatchdogAdapter_Init(stm32_watchdog_adapter_t *adapter);

/**
 * @brief Get the watchdog interface owned by an adapter.
 *
 * @param[in] adapter Initialized adapter, or NULL.
 *
 * @return The adapter-owned interface, or NULL when @p adapter is NULL.
 *
 * @note The returned pointer remains valid only while @p adapter remains valid.
 */
const watchdog_t *STM32WatchdogAdapter_Interface(
    const stm32_watchdog_adapter_t *adapter);

#endif
