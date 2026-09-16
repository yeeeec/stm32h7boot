#include "platform/platform_application.h"

#include <stddef.h>

#include "main.h"
#include "platform_flash.h"

#define PLATFORM_APPLICATION_VECTOR_ALIGNMENT 128U

typedef void (*application_entry_t)(void);

static int IsValidStackPointer(uint32_t stack_pointer)
{
    const int in_dtcm     = (stack_pointer >= 0x20000000UL) && (stack_pointer < 0x20020000UL);
    const int in_axi_sram = (stack_pointer >= 0x24000000UL) && (stack_pointer < 0x24080000UL);
    const int in_d2_sram  = (stack_pointer >= 0x30000000UL) && (stack_pointer < 0x30048000UL);
    const int in_sram4    = (stack_pointer >= 0x38000000UL) && (stack_pointer < 0x38010000UL);

    return (((stack_pointer & 0x7U) == 0U) && (in_dtcm || in_axi_sram || in_d2_sram || in_sram4));
}

static int IsValidResetHandler(uint32_t reset_handler)
{
    uint32_t address = reset_handler & ~1UL;
    uint32_t limit   = PLATFORM_FLASH_MAPPED_BASE + PLATFORM_FLASH_CAPACITY_BYTES;

    return (((reset_handler & 1U) != 0U) && (address >= PLATFORM_FLASH_MAPPED_BASE) &&
            (address < limit));
}

firmware_status_t PlatformApplication_Validate(uint32_t vector_address)
{
    const uint32_t mapped_limit = PLATFORM_FLASH_MAPPED_BASE + PLATFORM_FLASH_CAPACITY_BYTES;
    const volatile uint32_t *vectors;
    uint32_t stack_pointer;
    uint32_t reset_handler;
    firmware_status_t status;

    if ((vector_address < PLATFORM_FLASH_MAPPED_BASE) ||
        (vector_address > (mapped_limit - (2U * sizeof(uint32_t)))) ||
        ((vector_address % PLATFORM_APPLICATION_VECTOR_ALIGNMENT) != 0U))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    status = PlatformFlash_EnterMemoryMapped();
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }

    vectors       = (const volatile uint32_t *) (uintptr_t) vector_address;
    stack_pointer = vectors[0];
    reset_handler = vectors[1];

    if (!IsValidStackPointer(stack_pointer) || !IsValidResetHandler(reset_handler))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FIRMWARE_STATUS_OK;
}

void PlatformApplication_Jump(uint32_t vector_address)
{
    const volatile uint32_t *vectors;
    uint32_t stack_pointer;
    uint32_t reset_handler;
    application_entry_t entry;
    uint32_t i;

    if (PlatformApplication_Validate(vector_address) != FIRMWARE_STATUS_OK)
    {
        return;
    }

    vectors       = (const volatile uint32_t *) (uintptr_t) vector_address;
    stack_pointer = vectors[0];
    reset_handler = vectors[1];
    entry         = (application_entry_t) (uintptr_t) reset_handler;

    __disable_irq();

    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;

    for (i = 0U; i < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0])); ++i)
    {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

#if (__DCACHE_PRESENT == 1U)
    SCB_CleanDCache();
#endif
#if (__ICACHE_PRESENT == 1U)
    SCB_InvalidateICache();
#endif

    SCB->VTOR = vector_address;
    __set_BASEPRI(0U);
    __set_FAULTMASK(0U);
    __set_CONTROL(0U);
    __set_PSP(0U);
    __set_MSP(stack_pointer);
    __DSB();
    __ISB();
    __enable_irq();

    entry();
}
