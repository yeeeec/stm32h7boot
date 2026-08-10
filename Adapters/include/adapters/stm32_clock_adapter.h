/**
 * @file stm32_clock_adapter.h
 * @brief 将 STM32 平台节拍适配为系统时钟接口。
 */
#ifndef STM32_CLOCK_ADAPTER_H
#define STM32_CLOCK_ADAPTER_H

#include "firmware/system_clock.h"

/** 保存由 STM32 HAL 节拍驱动的系统时钟接口。 */
typedef struct
{
    /** 对外暴露的系统时钟回调表。 */
    system_clock_t interface;
} stm32_clock_adapter_t;

/**
 * @brief 初始化 STM32 系统时钟适配器。
 *
 * @param[out] adapter 待初始化的适配器实例；NULL 参数会被忽略。
 */
void STM32ClockAdapter_Init(stm32_clock_adapter_t *adapter);

/**
 * @brief 获取适配器持有的系统时钟接口。
 *
 * @param[in] adapter 已初始化的适配器，或 NULL。
 *
 * @return 适配器持有的接口；@p adapter 为 NULL 时返回 NULL。
 *
 * @note 返回指针仅在 @p adapter 有效期间保持有效。
 */
const system_clock_t *STM32ClockAdapter_Interface(
    const stm32_clock_adapter_t *adapter);

#endif
