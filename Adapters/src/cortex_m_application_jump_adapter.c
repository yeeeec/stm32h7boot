/**
 * @file cortex_m_application_jump_adapter.c
 * @brief Cortex-M7 interrupt, vector-table, stack, and branch handoff sequence.
 */
#include "adapters/cortex_m_application_jump_adapter.h"

#include <stddef.h>
#include <stdint.h>

#include "stm32h7xx.h"

typedef void (*application_entry_t)(void);

static firmware_status_t Execute(void *context, uint32_t vector_table_address)
{
    uint32_t initial_msp;
    uint32_t reset_handler;
    uint32_t index;
    application_entry_t entry;

    if ((context == NULL) || ((vector_table_address & 0x7FU) != 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    initial_msp = *(const volatile uint32_t *)(uintptr_t)vector_table_address;
    reset_handler =
        *(const volatile uint32_t *)(uintptr_t)(vector_table_address + 4U);
    entry = (application_entry_t)(uintptr_t)reset_handler;

    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    for (index = 0U; index < 8U; ++index)
    {
        NVIC->ICER[index] = UINT32_MAX;
        NVIC->ICPR[index] = UINT32_MAX;
    }
    SCB->VTOR = vector_table_address;
    __DSB();
    __ISB();
    __set_MSP(initial_msp);
    entry();

    return FIRMWARE_STATUS_INVALID_STATE;
}

firmware_status_t CortexMApplicationJumpAdapter_Init(
    cortex_m_application_jump_adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    adapter->interface.context = adapter;
    adapter->interface.execute = Execute;
    return FIRMWARE_STATUS_OK;
}

const application_jump_t *CortexMApplicationJumpAdapter_Interface(
    const cortex_m_application_jump_adapter_t *adapter)
{
    return (adapter == NULL) ? NULL : &adapter->interface;
}
