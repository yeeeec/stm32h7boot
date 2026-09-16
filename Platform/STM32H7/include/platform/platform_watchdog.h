#ifndef PLATFORM_WATCHDOG_H
#define PLATFORM_WATCHDOG_H

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef firmware_status_t (*platform_watchdog_kick_fn)(void *context);
    void PlatformWatchdog_Bind(platform_watchdog_kick_fn kick, void *context);
    firmware_status_t PlatformWatchdog_Kick(void);

#ifdef __cplusplus
}
#endif

#endif
