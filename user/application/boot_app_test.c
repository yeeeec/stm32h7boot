#include "boot_app.h"

#include "boot_config.h"
#include "boot_simple_jump.h"
#include "platform/boot_platform.h"

static uint8_t g_boot_test_started;

void Boot_App_Init(void) {
    g_boot_test_started = 0U;
}

void Boot_App_Process(void) {
    if (g_boot_test_started != 0U) {
        Boot_Platform_FeedWatchdog();
        return;
    }

    g_boot_test_started = 1U;

    (void)Boot_SimpleJump_ToAddressUnchecked(BOOT_DEFAULT_APP_BASE);

    while (1) {
        Boot_Platform_FeedWatchdog();
    }
}
