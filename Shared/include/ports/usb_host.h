#ifndef FIRMWARE_USB_HOST_H
#define FIRMWARE_USB_HOST_H

#include <stdbool.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Optional host facade. USB Host is intentionally not linked by this
 * bare-metal boot target, but the contract is shared with the application. */
firmware_status_t UsbHost_Init(void);
firmware_status_t UsbHost_Process(void);
bool UsbHost_IsReady(void);

#ifdef __cplusplus
}
#endif

#endif
