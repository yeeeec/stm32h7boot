/**
 * @file platform_reset.c
 * @brief STM32H7 Fail-closed 全系统 Reset 序列。
 *
 * 触发 SYSRESETREQ 前屏蔽中断并完成未结束写入。如果硬件延迟 Reset，终止
 * 循环会阻止调用者基于无效生命周期假设恢复正常 Boot。
 */
#include "platform/platform_reset.h"

#include "stm32h7xx.h"

void Platform_Reset(void)
{
    __disable_irq();
    __DSB();
    NVIC_SystemReset();

    for (;;)
    {
    }
}
