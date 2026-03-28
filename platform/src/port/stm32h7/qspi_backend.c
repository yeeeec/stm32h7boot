#include "platform/qspi.h"

#include <stddef.h>

#include "main.h"

extern QSPI_HandleTypeDef hqspi;

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

bool platform_qspi_is_ready(void) {
    return hqspi.Instance != NULL;
}

QSPI_HandleTypeDef *platform_qspi_get_handle(void) {
    return &hqspi;
}

Plat_Status_t platform_qspi_command(QSPI_CommandTypeDef *command, uint32_t timeout) {
    if (command == NULL) {
        return PLAT_ERR_INVALID_PARAM;
    }

    return platform_status_from_hal(HAL_QSPI_Command(&hqspi, command, timeout));
}

Plat_Status_t platform_qspi_transmit(uint8_t *data, uint32_t timeout) {
    if (data == NULL) {
        return PLAT_ERR_INVALID_PARAM;
    }

    return platform_status_from_hal(HAL_QSPI_Transmit(&hqspi, data, timeout));
}

Plat_Status_t platform_qspi_receive(uint8_t *data, uint32_t timeout) {
    if (data == NULL) {
        return PLAT_ERR_INVALID_PARAM;
    }

    return platform_status_from_hal(HAL_QSPI_Receive(&hqspi, data, timeout));
}

Plat_Status_t platform_qspi_auto_polling(QSPI_CommandTypeDef *command,
                                         QSPI_AutoPollingTypeDef *config,
                                         uint32_t timeout) {
    if ((command == NULL) || (config == NULL)) {
        return PLAT_ERR_INVALID_PARAM;
    }

    return platform_status_from_hal(HAL_QSPI_AutoPolling(&hqspi, command, config, timeout));
}

Plat_Status_t platform_qspi_memory_mapped(QSPI_CommandTypeDef *command,
                                          QSPI_MemoryMappedTypeDef *config) {
    if ((command == NULL) || (config == NULL)) {
        return PLAT_ERR_INVALID_PARAM;
    }

    return platform_status_from_hal(HAL_QSPI_MemoryMapped(&hqspi, command, config));
}
