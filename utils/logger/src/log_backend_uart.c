/* utils/logger/src/log_backend_uart.c */
#include "log_backend_uart.h"

#if LOG_ENABLE && LOG_BACKEND_UART

#include "platform/uart.h"

#ifndef PL_LOG_UART_ID
#define PL_LOG_UART_ID 0
#endif

void log_backend_uart_init(void) {
    /* Optional: init banner */
}

void log_backend_uart_output(const char *data, uint16_t len) {
    if (!data || len == 0) return;
    while (platform_uart_is_tx_busy(PL_LOG_UART_ID)) {}
    platform_uart_send(PL_LOG_UART_ID, (const uint8_t*)data, len);
}

#else

void log_backend_uart_init(void) {}
void log_backend_uart_output(const char *data, uint16_t len) { (void)data; (void)len; }

#endif
