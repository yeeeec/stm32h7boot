#include "platform/platform_watchdog.h"

static platform_watchdog_kick_fn s_kick;
static void *s_context;

void PlatformWatchdog_Bind(platform_watchdog_kick_fn kick, void *context)
{
    s_kick    = kick;
    s_context = context;
}
firmware_status_t PlatformWatchdog_Kick(void)
{
    return s_kick == 0 ? FIRMWARE_STATUS_INVALID_STATE : s_kick(s_context);
}
