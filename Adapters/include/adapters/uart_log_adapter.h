/**
 * @file uart_log_adapter.h
 * @brief Adapter from the BSP debug UART to the generic log-sink interface.
 */
#ifndef UART_LOG_ADAPTER_H
#define UART_LOG_ADAPTER_H

#include "firmware/log_sink.h"

/** Owns a log-sink interface backed by the board debug UART. */
typedef struct
{
    log_sink_t interface;
} uart_log_adapter_t;

/**
 * @brief Initialize a UART log-sink adapter.
 *
 * @param[out] adapter Adapter instance to initialize; NULL is ignored.
 */
void UartLogAdapter_Init(uart_log_adapter_t *adapter);

/**
 * @brief Get the log-sink interface owned by an adapter.
 *
 * @param[in] adapter Initialized adapter, or NULL.
 *
 * @return The adapter-owned interface, or NULL when @p adapter is NULL.
 *
 * @note The returned pointer remains valid only while @p adapter remains valid.
 */
const log_sink_t *UartLogAdapter_Interface(const uart_log_adapter_t *adapter);

#endif
