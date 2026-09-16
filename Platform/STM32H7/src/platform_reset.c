#include "platform/platform_reset.h"

static platform_reset_fn s_reset;
static void *s_context;

void PlatformReset_Bind(platform_reset_fn reset, void *context)
{
    s_reset   = reset;
    s_context = context;
}
void PlatformReset_Request(void)
{
    if (s_reset != 0)
        s_reset(s_context);
}
