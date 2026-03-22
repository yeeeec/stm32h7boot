#include "svc_io.h"

#include "logging.h"
#include "platform/fmc_io.h"

enum {
    SVC_IO_LED_MASK = PLAT_FMC_IO_PIN_MASK(PLAT_FMC_IO_LED1) |
                      PLAT_FMC_IO_PIN_MASK(PLAT_FMC_IO_LED2) |
                      PLAT_FMC_IO_PIN_MASK(PLAT_FMC_IO_LED3) |
                      PLAT_FMC_IO_PIN_MASK(PLAT_FMC_IO_LED4)
};

static bool s_io_service_ready = false;

void svc_io_init(void) {
    s_io_service_ready = platform_fmc_io_is_ready();

    if (s_io_service_ready) {
        LOG_INFO("svc", "io service enabled, LED1~4 periodic toggle");
    } else {
        LOG_ERROR("svc", "io service disabled: platform io not ready");
    }
}

void svc_io_process(void) {
    uint32_t next_state;

    if (!s_io_service_ready) {
        return;
    }

    next_state = platform_fmc_io_get_shadow_state() ^ SVC_IO_LED_MASK;
    if (platform_fmc_io_write_port(next_state) != PLAT_OK) {
        s_io_service_ready = false;
        LOG_ERROR("svc", "io write failed, LED toggle stopped");
    }
}
