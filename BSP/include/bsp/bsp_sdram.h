/**
 * @file bsp_sdram.h
 * @brief Board SDRAM initialization through the FMC controller.
 */
#ifndef BSP_SDRAM_H
#define BSP_SDRAM_H

#include "firmware/status.h"

/**
 * @brief Initialize the board SDRAM timing and mode configuration.
 *
 * @return FIRMWARE_STATUS_OK on success.
 * @return FIRMWARE_STATUS_INVALID_STATE when the CubeMX SDRAM handle is reset.
 * @return A controller or transport failure status otherwise.
 *
 * @pre The generated FMC/SDRAM handle has been initialized.
 */
firmware_status_t BSP_SdramInit(void);

#endif
