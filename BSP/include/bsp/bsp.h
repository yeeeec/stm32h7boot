#ifndef BSP_H
#define BSP_H

#include "firmware/status.h"

firmware_status_t BSP_Init(void);
int BSP_IsInitialized(void);

#endif
