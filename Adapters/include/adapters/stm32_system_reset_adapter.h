/**
 * @file stm32_system_reset_adapter.h
 * @brief STM32 implementation of the system reset interface.
 */
#ifndef ADAPTERS_STM32_SYSTEM_RESET_ADAPTER_H
#define ADAPTERS_STM32_SYSTEM_RESET_ADAPTER_H

#include "firmware/status.h"
#include "firmware/system_reset.h"

typedef struct
{
    system_reset_t interface;
    int initialized;
} stm32_system_reset_adapter_t;

/** Bind the platform reset primitive to a stable firmware interface. */
firmware_status_t Stm32SystemResetAdapter_Init(
    stm32_system_reset_adapter_t *adapter);

/** Return the interface owned by an initialized adapter. */
const system_reset_t *Stm32SystemResetAdapter_Interface(
    const stm32_system_reset_adapter_t *adapter);

#endif
