#include "platform/system.h"

#include "platform/config.h"
#include "platform/fmc.h"
#include "platform/fmc_io.h"

static bool s_platform_ready = false;

void platform_init(void) {
    Plat_Status_t io_status = platform_fmc_io_init();
    Plat_Status_t sdram_status = platform_fmc_sdram_init();

    s_platform_ready = (io_status == PLAT_OK) && (sdram_status == PLAT_OK);
}

void platform_process(void) {
}

bool platform_is_ready(void) {
    return s_platform_ready;
}

const char *platform_get_name(void) {
    return PLATFORM_NAME;
}
