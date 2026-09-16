#ifndef BSP_ISP_GPIO_H
#define BSP_ISP_GPIO_H

#include "firmware/status.h"

void BspIspGpio_Init(void);

firmware_status_t BspIspGpio_SetBoot0(int high);

firmware_status_t BspIspGpio_ResetTarget(void);

#endif
