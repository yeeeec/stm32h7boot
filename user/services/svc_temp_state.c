#include "svc_temp_state.h"

static uint32_t s_poll_count = 0u;

void svc_temp_state_reset(void) {
    s_poll_count = 0u;
}

void svc_temp_state_set_poll_count(uint32_t poll_count) {
    s_poll_count = poll_count;
}

uint32_t svc_temp_state_get_poll_count(void) {
    return s_poll_count;
}
