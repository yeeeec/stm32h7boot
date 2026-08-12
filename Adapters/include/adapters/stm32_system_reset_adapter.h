/**
 * @file stm32_system_reset_adapter.h
 * @brief STM32 系统复位接口适配实现。
 */
#ifndef ADAPTERS_STM32_SYSTEM_RESET_ADAPTER_H
#define ADAPTERS_STM32_SYSTEM_RESET_ADAPTER_H

#include "firmware/status.h"
#include "firmware/system_reset.h"

/** 保存 STM32 平台复位回调及初始化状态的适配器。 */
typedef struct
{
    /** 适配器持有的系统复位接口。 */
    system_reset_t interface;
    /** 标记适配器是否已经完成初始化。 */
    int initialized;
} stm32_system_reset_adapter_t;

/** 将平台复位原语绑定到稳定的固件接口。 */
firmware_status_t Stm32SystemResetAdapter_Init(stm32_system_reset_adapter_t *adapter);

/** 返回已初始化适配器持有的接口；未初始化或参数为 NULL 时返回 NULL。 */
const system_reset_t *
Stm32SystemResetAdapter_Interface(const stm32_system_reset_adapter_t *adapter);

#endif
