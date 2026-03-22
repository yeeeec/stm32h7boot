#include "svc_temp.h"

#include "logging.h"
#include "svc_temp_state.h"

void svc_temp_init(void) {
    svc_temp_state_reset();
    LOG_INFO("svc", "service temp skeleton initialized");
}

void svc_temp_process(void) {
    static uint32_t s_poll_count = 0u;

    ++s_poll_count;
    svc_temp_state_set_poll_count(s_poll_count);
}
