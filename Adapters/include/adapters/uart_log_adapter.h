#ifndef UART_LOG_ADAPTER_H
#define UART_LOG_ADAPTER_H

#include "firmware/log_sink.h"

typedef struct
{
    log_sink_t interface;
} uart_log_adapter_t;

void UartLogAdapter_Init(uart_log_adapter_t *adapter);
const log_sink_t *UartLogAdapter_Interface(const uart_log_adapter_t *adapter);

#endif
