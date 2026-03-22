#include "app_temp.h"

#include "app_temp_state.h"
#include "logging.h"
#include "platform/fmc.h"
#include "platform/ltdc.h"
#include "platform/qspi.h"
#include "platform/system.h"

void app_temp_init(void) {
    app_temp_state_reset();

    LOG_INFO("app", "application temp skeleton initialized on %s", platform_get_name());
    LOG_INFO("app", "peripheral wrappers ready: FMC=%u LTDC=%u QSPI=%u",
             platform_fmc_is_ready() ? 1u : 0u, platform_ltdc_is_ready() ? 1u : 0u,
             platform_qspi_is_ready() ? 1u : 0u);
}

void app_temp_process(void) {
    uint32_t heartbeat = app_temp_state_step();

    if ((heartbeat % 5u) == 0u) {
        LOG_INFO("app", "heartbeat %lu", (unsigned long) heartbeat);
    }
}
