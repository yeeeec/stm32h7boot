/**
 * @file bsp.h
 * @brief Board-support-package lifecycle entry points.
 */
#ifndef BSP_H
#define BSP_H

#include "firmware/status.h"

/**
 * @brief Initialize board peripherals used by the firmware.
 *
 * SDRAM, external QSPI flash, and the configured AT24C128 EEPROM are
 * initialized in that order. Generated UART, FMC, QSPI, and I2C handles must
 * already be initialized by CubeMX startup code.
 *
 * @return FIRMWARE_STATUS_OK on success.
 * @return FIRMWARE_STATUS_INVALID_STATE if called twice or a required HAL
 *         peripheral is still in reset state.
 */
firmware_status_t BSP_Init(void);

/**
 * @brief Query whether BSP_Init completed successfully.
 *
 * @return Nonzero after successful initialization; zero otherwise.
 */
int BSP_IsInitialized(void);

#endif
