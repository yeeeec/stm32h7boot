/* utils/logger/inc/log_backend_uart.h */
#ifndef LOG_BACKEND_UART_H
#define LOG_BACKEND_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void log_backend_uart_init(void);
void log_backend_uart_output(const char *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* LOG_BACKEND_UART_H */
