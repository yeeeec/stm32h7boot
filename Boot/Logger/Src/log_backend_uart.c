#include "log_backend_uart.h"
#include "logging.h"

#if LOG_ENABLE && LOG_BACKEND_UART

#include "main.h"
#include "stm32h7xx_hal.h"

extern UART_HandleTypeDef huart1;

void log_backend_uart_init(void)
{
}

void log_backend_uart_output(const char *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U)) {
        return;
    }

    if ((huart1.Instance != USART1) || (huart1.gState == HAL_UART_STATE_RESET)) {
        return;
    }

    (void)HAL_UART_Transmit(&huart1, (uint8_t *)data, len, 20U);
}

#else

void log_backend_uart_init(void) {}
void log_backend_uart_output(const char *data, uint16_t len) { (void)data; (void)len; }

#endif
