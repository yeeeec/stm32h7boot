#ifndef TEST_FAKE_BSP_DRIVER_SD_H
#define TEST_FAKE_BSP_DRIVER_SD_H

#include <stdint.h>

#define SD_PRESENT ((uint8_t)1U)

uint8_t BSP_SD_IsDetected(void);

#endif
