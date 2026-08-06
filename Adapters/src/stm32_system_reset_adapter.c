/**
 * @file stm32_system_reset_adapter.c
 * @brief STM32 implementation of the system reset interface.
 */
#include "adapters/stm32_system_reset_adapter.h"

#include <stddef.h>

#include "platform/platform_reset.h"

static void RequestReset(void *context)
{
    (void)context;
    Platform_Reset();
}

firmware_status_t Stm32SystemResetAdapter_Init(
    stm32_system_reset_adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    adapter->interface.context = adapter;
    adapter->interface.request = RequestReset;
    adapter->initialized = 1;
    return FIRMWARE_STATUS_OK;
}

const system_reset_t *Stm32SystemResetAdapter_Interface(
    const stm32_system_reset_adapter_t *adapter)
{
    return ((adapter == NULL) || (adapter->initialized == 0))
               ? NULL
               : &adapter->interface;
}
