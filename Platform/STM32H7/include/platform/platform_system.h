#ifndef PLATFORM_SYSTEM_H
#define PLATFORM_SYSTEM_H

#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define PLATFORM_RESET_CAUSE_SOFTWARE  (1UL << 0U)
#define PLATFORM_RESET_CAUSE_WATCHDOG  (1UL << 1U)
#define PLATFORM_RESET_CAUSE_POWER_ON  (1UL << 2U)
#define PLATFORM_RESET_CAUSE_BROWN_OUT (1UL << 3U)
#define PLATFORM_RESET_CAUSE_PIN       (1UL << 4U)

    uint32_t PlatformSystem_GetMs(void);
    void PlatformSystem_DelayMs(uint32_t delay_ms);

    firmware_status_t PlatformSystem_WatchdogInit(void);
    void PlatformSystem_WatchdogRefresh(void);

    uint32_t PlatformSystem_GetResetCause(void);
    void PlatformSystem_ClearResetCause(void);

    void PlatformSystem_Reset(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_SYSTEM_H */
