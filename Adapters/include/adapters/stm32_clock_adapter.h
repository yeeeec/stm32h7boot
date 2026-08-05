/**
 * @file stm32_clock_adapter.h
 * @brief Adapter from the STM32 platform tick to the system-clock interface.
 */
#ifndef STM32_CLOCK_ADAPTER_H
#define STM32_CLOCK_ADAPTER_H

#include "firmware/system_clock.h"

/** Owns a system-clock interface backed by the STM32 HAL tick. */
typedef struct
{
    system_clock_t interface;
} stm32_clock_adapter_t;

/**
 * @brief Initialize an STM32 system-clock adapter.
 *
 * @param[out] adapter Adapter instance to initialize; NULL is ignored.
 */
void STM32ClockAdapter_Init(stm32_clock_adapter_t *adapter);

/**
 * @brief Get the system-clock interface owned by an adapter.
 *
 * @param[in] adapter Initialized adapter, or NULL.
 *
 * @return The adapter-owned interface, or NULL when @p adapter is NULL.
 *
 * @note The returned pointer remains valid only while @p adapter remains valid.
 */
const system_clock_t *STM32ClockAdapter_Interface(
    const stm32_clock_adapter_t *adapter);

#endif
