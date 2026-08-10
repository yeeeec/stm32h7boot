/**
 * @file stm32_watchdog_adapter.c
 * @brief 基于 STM32 平台看门狗实现通用看门狗接口。
 */
#include "adapters/stm32_watchdog_adapter.h"

#include <stddef.h>

#include "platform/platform_watchdog.h"

/** 将看门狗刷新请求转发到平台层。 */
static firmware_status_t WatchdogRefresh(void *context)
{
    (void)context;
    return Platform_WatchdogRefresh();
}

void STM32WatchdogAdapter_Init(stm32_watchdog_adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return;
    }

    /* 平台看门狗由全局 HAL 实例管理，不需要额外上下文。 */
    adapter->interface.context = NULL;
    adapter->interface.refresh = WatchdogRefresh;
}

const watchdog_t *STM32WatchdogAdapter_Interface(
    const stm32_watchdog_adapter_t *adapter)
{
    /* 返回适配器内嵌的接口。 */
    return (adapter == NULL) ? NULL : &adapter->interface;
}
