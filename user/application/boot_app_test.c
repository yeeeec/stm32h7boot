#include "boot_app.h"

#include "boot_config.h"
#include "boot_error.h"
#include "boot_log.h"
#include "boot_simple_jump.h"

static uint8_t g_boot_test_started;
static BootError g_boot_test_last_error;

void Boot_App_Init(void) {
    g_boot_test_started    = 0U;
    g_boot_test_last_error = BOOT_ERR_NONE;
}

void Boot_App_Process(void) {
    if (g_boot_test_started != 0U) {
        return;
    }

    g_boot_test_started = 1U;
    LOG_INFO(BOOT_LOG_TAG, "Test jump to app base=0x%08lX", (unsigned long)BOOT_DEFAULT_APP_BASE);

    g_boot_test_last_error = Boot_SimpleJump_ToAddressUnchecked(BOOT_DEFAULT_APP_BASE);
    LOG_WARN(BOOT_LOG_TAG, "Test jump failed: %s", Boot_ErrorToString(g_boot_test_last_error));

    while (1) {
    }
}
