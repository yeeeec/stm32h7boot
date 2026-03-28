#include "platform/uart.h"

#include "main.h"

#ifndef PLATFORM_UART_TX_TIMEOUT_MS
#define PLATFORM_UART_TX_TIMEOUT_MS 100U
#endif

extern UART_HandleTypeDef huart1;

static Plat_Status_t platform_status_from_hal(HAL_StatusTypeDef status) {
    switch (status) {
        case HAL_OK:
            return PLAT_OK;
        case HAL_TIMEOUT:
            return PLAT_ERR_TIMEOUT;
        case HAL_BUSY:
            return PLAT_ERR_BUSY;
        default:
            return PLAT_ERR_HW_FAILURE;
    }
}

static UART_HandleTypeDef *platform_uart_get_handle(Plat_UART_ID_t id) {
    switch (id) {
        case PLAT_UART_DISPLAY:
            return (huart1.Instance != NULL) ? &huart1 : NULL;
        default:
            return NULL;
    }
}

Plat_Status_t platform_uart_init(Plat_UART_ID_t id, uint32_t baud_rate) {
    UART_HandleTypeDef *handle;

    if (id >= PLAT_UART_MAX) {
        return PLAT_ERR_INVALID_PARAM;
    }

    handle = platform_uart_get_handle(id);
    if (handle == NULL) {
        return PLAT_ERR_HW_FAILURE;
    }

    if ((baud_rate != 0U) && (handle->Init.BaudRate != baud_rate)) {
        return PLAT_ERR_INVALID_PARAM;
    }

    return PLAT_OK;
}

Plat_Status_t platform_uart_send_data(Plat_UART_ID_t id, const uint8_t *data, uint16_t len) {
    UART_HandleTypeDef *handle;

    if ((id >= PLAT_UART_MAX) || (data == NULL) || (len == 0U)) {
        return PLAT_ERR_INVALID_PARAM;
    }

    handle = platform_uart_get_handle(id);
    if (handle == NULL) {
        return PLAT_ERR_HW_FAILURE;
    }

    return platform_status_from_hal(
        HAL_UART_Transmit(handle, (uint8_t *) data, len, PLATFORM_UART_TX_TIMEOUT_MS));
}

bool platform_uart_is_tx_busy(Plat_UART_ID_t id) {
    HAL_UART_StateTypeDef state;
    UART_HandleTypeDef *handle;

    if (id >= PLAT_UART_MAX) {
        return false;
    }

    handle = platform_uart_get_handle(id);
    if (handle == NULL) {
        return false;
    }

    state = HAL_UART_GetState(handle);
    return (state == HAL_UART_STATE_BUSY_TX) || (state == HAL_UART_STATE_BUSY_TX_RX);
}

void platform_uart_register_rx_rb(Plat_UART_ID_t id, lwrb_t *rb) {
    (void) id;
    (void) rb;
}

uint16_t platform_uart_read_data(Plat_UART_ID_t id, uint8_t *data, uint16_t len) {
    (void) id;
    (void) data;
    (void) len;
    return 0U;
}
