#include "platform/platform_cpu.h"

#include "main.h"
#include "platform/platform_memory_map.h"

typedef void (*platform_cpu_entry_t)(void);

bool PlatformCpu_IsValidStackPointer(uint32_t stack_pointer)
{
    const bool in_dtcm =
        (stack_pointer > PLATFORM_DTCM_BASE) && (stack_pointer <= PLATFORM_DTCM_END);
    const bool in_axi =
        (stack_pointer > PLATFORM_AXI_SRAM_BASE) && (stack_pointer <= PLATFORM_AXI_SRAM_END);
    const bool in_d2 =
        (stack_pointer > PLATFORM_D2_SRAM_BASE) && (stack_pointer <= PLATFORM_D2_SRAM_END);
    const bool in_sram4 =
        (stack_pointer > PLATFORM_SRAM4_BASE) && (stack_pointer <= PLATFORM_SRAM4_END);
    return ((stack_pointer & 0x7U) == 0U) && (in_dtcm || in_axi || in_d2 || in_sram4);
}

_Noreturn void PlatformCpu_Jump(uint32_t vector_address)
{
    const volatile uint32_t *vectors = (const volatile uint32_t *) (uintptr_t) vector_address;
    const uint32_t stack_pointer     = vectors[0];
    const uint32_t reset_handler     = vectors[1];
    platform_cpu_entry_t entry       = (platform_cpu_entry_t) (uintptr_t) reset_handler;
    uint32_t i;

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
    entry();
    for (;;)
    {
    }
}
