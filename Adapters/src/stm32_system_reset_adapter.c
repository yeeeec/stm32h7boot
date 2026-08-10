/**
 * @file stm32_system_reset_adapter.c
 * @brief 基于 STM32 平台复位原语实现系统复位接口。
 */
#include "adapters/stm32_system_reset_adapter.h"

#include <stddef.h>

#include "platform/platform_reset.h"

/** 转发系统复位请求到平台层；该调用通常不会返回。 */
static void RequestReset(void *context)
{
    (void)context;
    Platform_Reset();
}

firmware_status_t Stm32SystemResetAdapter_Init(
    stm32_system_reset_adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    /* 记录初始化状态，避免重复绑定回调表。 */
    adapter->interface.context = adapter;
    adapter->interface.request = RequestReset;
    adapter->initialized = 1;
    return FIRMWARE_STATUS_OK;
}

const system_reset_t *Stm32SystemResetAdapter_Interface(
    const stm32_system_reset_adapter_t *adapter)
{
    /* 未初始化的适配器不向服务层暴露不完整接口。 */
    return ((adapter == NULL) || (adapter->initialized == 0))
               ? NULL
               : &adapter->interface;
}
