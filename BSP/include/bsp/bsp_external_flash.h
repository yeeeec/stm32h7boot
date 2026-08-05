#ifndef BSP_EXTERNAL_FLASH_H
#define BSP_EXTERNAL_FLASH_H

#include "firmware/status.h"

struct spi_nor;

firmware_status_t BSP_ExternalFlashInit(void);
struct spi_nor *BSP_ExternalFlashDevice(void);

#endif
