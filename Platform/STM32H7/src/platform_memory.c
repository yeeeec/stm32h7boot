/**
 * @file platform_memory.c
 * @brief 基于 CMSIS 的 ARM 访问顺序原语。
 *
 * 将 Barrier 封装在 Platform 后，使 Architecture 调用点不依赖 CMSIS，同时
 * 保留 DMB、DSB 和 ISB 的精确语义。
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
