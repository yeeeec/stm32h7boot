#ifndef BSP_THERAPY_GPIO_H
#define BSP_THERAPY_GPIO_H

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    firmware_status_t BspTherapyGpio_Init(void);
    firmware_status_t BspTherapy_SetBoot0(int high);
    firmware_status_t BspTherapy_SetReset(int asserted);

#ifdef __cplusplus
}
#endif

#endif /* BSP_THERAPY_GPIO_H */
