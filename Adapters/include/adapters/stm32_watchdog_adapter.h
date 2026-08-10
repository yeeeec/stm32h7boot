/**
 * @file stm32_watchdog_adapter.h
 * @brief 将 STM32 平台看门狗适配为通用看门狗接口。
 */
#ifndef STM32_WATCHDOG_ADAPTER_H
#define STM32_WATCHDOG_ADAPTER_H

#include "firmware/watchdog.h"

/** 保存由配置的 STM32 IWDG 实例驱动的看门狗接口。 */
typedef struct
{
    /** 对外暴露的看门狗回调表。 */
    watchdog_t interface;
} stm32_watchdog_adapter_t;

/**
 * @brief 初始化 STM32 看门狗适配器。
 *
 * @param[out] adapter 待初始化的适配器实例；NULL 参数会被忽略。
 */
void STM32WatchdogAdapter_Init(stm32_watchdog_adapter_t *adapter);

/**
 * @brief 获取适配器持有的看门狗接口。
 *
 * @param[in] adapter 已初始化的适配器，或 NULL。
 *
 * @return 适配器持有的接口；@p adapter 为 NULL 时返回 NULL。
 *
 * @note 返回指针仅在 @p adapter 有效期间保持有效。
 */
const watchdog_t *STM32WatchdogAdapter_Interface(
    const stm32_watchdog_adapter_t *adapter);

#endif
