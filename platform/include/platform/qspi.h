#ifndef PLATFORM_QSPI_H
#define PLATFORM_QSPI_H

#include "definitions.h"
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

bool platform_qspi_is_ready(void);
QSPI_HandleTypeDef *platform_qspi_get_handle(void);
Plat_Status_t platform_qspi_command(QSPI_CommandTypeDef *command, uint32_t timeout);
Plat_Status_t platform_qspi_transmit(uint8_t *data, uint32_t timeout);
Plat_Status_t platform_qspi_receive(uint8_t *data, uint32_t timeout);
Plat_Status_t platform_qspi_memory_mapped(QSPI_CommandTypeDef *command,
                                          QSPI_MemoryMappedTypeDef *config);

#ifdef __cplusplus
}
#endif

#endif
