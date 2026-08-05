/**
 * @file platform_memory.c
 * @brief ARM memory and instruction barrier implementation.
 */
#include "platform/platform_memory.h"

#include "stm32h7xx.h"

void Platform_MemoryDataBarrier(void)
{
    __DMB();
}

void Platform_MemorySyncBarrier(void)
{
    __DSB();
}

void Platform_MemoryInstructionBarrier(void)
{
    __ISB();
}
