#include "platform/platform_runtime.h"

#include "main.h"
#include "platform/platform_flash.h"
#include "platform/platform_memory_map.h"

typedef void (*application_entry_t)(void);

static int IsValidStackPointer(uint32_t stack_pointer)
{
    const int in_dtcm = (stack_pointer >= PLATFORM_DTCM_BASE) &&
                        (stack_pointer < PLATFORM_DTCM_END);
    const int in_axi = (stack_pointer >= PLATFORM_AXI_SRAM_BASE) &&
                       (stack_pointer < PLATFORM_AXI_SRAM_END);
    const int in_d2 = (stack_pointer >= PLATFORM_D2_SRAM_BASE) &&
                      (stack_pointer < PLATFORM_D2_SRAM_END);
    const int in_sram4 = (stack_pointer >= PLATFORM_SRAM4_BASE) &&
                         (stack_pointer < PLATFORM_SRAM4_END);
    return ((stack_pointer & 0x7U) == 0U) && (in_dtcm || in_axi || in_d2 || in_sram4);
}

static int IsValidResetHandler(uint32_t reset_handler, uint32_t region_start,
                               uint32_t region_end)
{
    uint32_t address = reset_handler & ~1UL;
    return ((reset_handler & 1U) != 0U) && (address >= region_start) && (address < region_end);
}

firmware_status_t PlatformRuntime_Validate(uint32_t vector_address, uint32_t region_size)
{
    const volatile uint32_t *vectors;
    uint32_t region_end;
    uint32_t stack_pointer;
    uint32_t reset_handler;

    if ((region_size < (2U * sizeof(uint32_t))) ||
        (vector_address < PLATFORM_QSPI_MAPPED_BASE) ||
        (region_size > PLATFORM_QSPI_MAPPED_SIZE) ||
        (vector_address > (PLATFORM_QSPI_MAPPED_BASE + PLATFORM_QSPI_MAPPED_SIZE - region_size)) ||
        ((vector_address % PLATFORM_APP_VECTOR_ALIGNMENT) != 0U))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    region_end = vector_address + region_size;
    {
        firmware_status_t status = PlatformFlash_EnterMemoryMapped();
        if (status != FIRMWARE_STATUS_OK)
        {
            return status;
        }
    }
    vectors = (const volatile uint32_t *)(uintptr_t) vector_address;
    stack_pointer = vectors[0];
    reset_handler = vectors[1];
    if (!IsValidStackPointer(stack_pointer) ||
        !IsValidResetHandler(reset_handler, vector_address, region_end))
    {
        (void) PlatformFlash_ExitMemoryMapped();
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FIRMWARE_STATUS_OK;
}

_Noreturn void PlatformRuntime_Start(uint32_t vector_address)
{
    const volatile uint32_t *vectors = (const volatile uint32_t *)(uintptr_t) vector_address;
    const uint32_t stack_pointer = vectors[0];
    const uint32_t reset_handler = vectors[1];
    application_entry_t entry = (application_entry_t)(uintptr_t) reset_handler;
    uint32_t i;

    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
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
    entry();
    for (;;)
    {
    }
}
