#ifndef BSP_H
#define BSP_H

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    firmware_status_t BSP_EarlyInit(void);
    firmware_status_t BSP_Init(void);
    int BSP_IsEarlyInitialized(void);
    int BSP_IsInitialized(void);

#ifdef __cplusplus
}
#endif

#endif
