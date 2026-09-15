#ifndef FIRMWARE_USB_HOST_H
#define FIRMWARE_USB_HOST_H

#include <stdbool.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* UpdateTask owns when the host is initialized and serviced. */
    firmware_status_t UsbHost_Init(void);
    firmware_status_t UsbHost_Process(void);
    bool UsbHost_IsReady(void);

#ifdef __cplusplus
}
#endif

#endif
