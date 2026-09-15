#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

#include "firmware/status.h"
#include "platform/platform_can.h"
#include "platform/platform_time.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Starts the CAN controller after BSP_CAN_Init has completed. This is the
     * sole application startup owner for CAN; Communication never starts it. */
    firmware_status_t Platform_Init(void);

#ifdef __cplusplus
}
#endif

#endif
