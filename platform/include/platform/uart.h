/** platform/include/platform/uart.h */
#ifndef PLATFORM_UART_H
#define PLATFORM_UART_H

#include <stdbool.h>

#include "definitions.h" /**< Contains Plat_Status_t, etc. */
#include "lwrb/lwrb.h"   /**< For lwrb_t registration */

#ifdef __cplusplus
extern "C" {
#endif

/** ---------------- Logical ID Definition ---------------- */
typedef enum {
    PLAT_UART_SENSOR = 0, /**< Maps to USART0 (Oxygen Sensor) */
    PLAT_UART_DISPLAY,    /**< Maps to USART1 (Display/App Comm) */
    PLAT_UART_MAX
} Plat_UART_ID_t;

/** ---------------- API Functions ---------------- */

/**
 * @brief Init channel.
 *
 * @param id
 * @param baud_rate
 * @return
 */
Plat_Status_t platform_uart_init(Plat_UART_ID_t id, uint32_t baud_rate);

/**
 * @brief Send data.
 *
 * @param id
 * @param data
 * @param len
 * @return
 */
Plat_Status_t platform_uart_send_data(Plat_UART_ID_t id, const uint8_t *data, uint16_t len);

/**
 * @brief Get tx busy.
 *
 * @param id
 * @return
 */
bool platform_uart_is_tx_busy(Plat_UART_ID_t id);

/**
 * @brief Register RX ring buffer (lwrb object) from service layer.
 *
 * Service layer owns the storage & lwrb_t object. Backend will write DMA RX
 * incremental data into the registered ring buffer.
 *
 * @param id
 * @param rb Pointer to initialized lwrb object.
 */
void platform_uart_register_rx_rb(Plat_UART_ID_t id, lwrb_t *rb);

/**
 * @brief Read data.
 *
 * @param id
 * @param data
 * @param len
 * @return
 */
uint16_t platform_uart_read_data(Plat_UART_ID_t id, uint8_t *data, uint16_t len);


#ifdef __cplusplus
}
#endif

#endif /**< PLATFORM_UART_H */
