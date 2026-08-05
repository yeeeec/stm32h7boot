/**
 * @file bsp_external_flash.h
 * @brief Board QSPI binding for the external SPI NOR flash.
 */
#ifndef BSP_EXTERNAL_FLASH_H
#define BSP_EXTERNAL_FLASH_H

#include "firmware/status.h"

struct spi_nor;

/**
 * @brief Initialize the board external SPI NOR flash.
 *
 * @return FIRMWARE_STATUS_OK when the device is identified and ready.
 * @return FIRMWARE_STATUS_INVALID_STATE when the CubeMX QSPI handle is reset.
 * @return A driver or transport failure status otherwise.
 *
 * @pre The generated QSPI and IWDG handles have been initialized.
 */
firmware_status_t BSP_ExternalFlashInit(void);

/**
 * @brief Return the initialized external flash driver instance.
 *
 * @return BSP-owned SPI NOR instance, or NULL before successful initialization.
 *
 * @note The returned object has static lifetime and must not be freed by callers.
 */
struct spi_nor *BSP_ExternalFlashDevice(void);

#endif
