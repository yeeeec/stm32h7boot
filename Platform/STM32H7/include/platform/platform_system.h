#ifndef PLATFORM_SYSTEM_H
#define PLATFORM_SYSTEM_H

#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    uint32_t PlatformSystem_GetMs(void);
    void PlatformSystem_DelayMs(uint32_t delay_ms);

    firmware_status_t PlatformSystem_WatchdogInit(void);
    void PlatformSystem_WatchdogRefresh(void);

    void PlatformSystem_Reset(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_SYSTEM_H */
