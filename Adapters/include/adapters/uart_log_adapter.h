/**
 * @file uart_log_adapter.h
 * @brief 将 BSP 调试串口适配为通用日志输出接口。
 */
#ifndef UART_LOG_ADAPTER_H
#define UART_LOG_ADAPTER_H

#include "firmware/log_sink.h"

/** 保存由板级调试串口驱动的日志输出接口。 */
typedef struct
{
    /** 对外暴露的日志输出回调表。 */
    log_sink_t interface;
} uart_log_adapter_t;

/**
 * @brief 初始化 UART 日志输出适配器。
 *
 * @param[out] adapter 待初始化的适配器实例；NULL 参数会被忽略。
 */
void UartLogAdapter_Init(uart_log_adapter_t *adapter);

/**
 * @brief 获取适配器持有的日志输出接口。
 *
 * @param[in] adapter 已初始化的适配器，或 NULL。
 *
 * @return 适配器持有的接口；@p adapter 为 NULL 时返回 NULL。
 *
 * @note 返回指针仅在 @p adapter 有效期间保持有效。
 */
const log_sink_t *UartLogAdapter_Interface(const uart_log_adapter_t *adapter);

#endif
