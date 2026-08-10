/**
 * @file stm32_clock_adapter.c
 * @brief 基于 STM32 平台节拍实现系统时钟接口。
 */
#include "adapters/stm32_clock_adapter.h"

#include <stddef.h>

#include "platform/platform_time.h"

/** 读取平台提供的单调毫秒计数。 */
static uint32_t ClockNowMs(void *context)
{
    (void)context;
    return Platform_TimeNowMs();
}

void STM32ClockAdapter_Init(stm32_clock_adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return;
    }

    /* 平台时钟不需要额外上下文，直接绑定统一时间函数。 */
    adapter->interface.context = NULL;
    adapter->interface.now_ms = ClockNowMs;
}

const system_clock_t *STM32ClockAdapter_Interface(
    const stm32_clock_adapter_t *adapter)
{
    /* 接口内嵌在适配器对象中。 */
    return (adapter == NULL) ? NULL : &adapter->interface;
}
