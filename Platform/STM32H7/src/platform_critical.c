/**
 * @file platform_critical.c
 * @brief 为可嵌套本地 Core Critical Section 保存 PRIMASK。
 *
 * 保存之前的屏蔽状态是不变量，可防止外层 Critical Section 仍持有排他边界
 * 时，内层 Section 提前启用中断。DMB 保证该边界两侧的共享内存访问顺序。
 */
#include "platform/platform_critical.h"

#include "stm32h7xx.h"

platform_critical_state_t Platform_CriticalEnter(void)
{
    platform_critical_state_t state = __get_PRIMASK();

    __disable_irq();
    __DMB();
    return state;
}

void Platform_CriticalExit(platform_critical_state_t state)
{
    __DMB();
    if ((state & 1U) == 0U)
    {
        __enable_irq();
    }
}
