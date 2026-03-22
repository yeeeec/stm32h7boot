#include "app_temp_state.h"

static uint32_t s_heartbeat = 0u;

void app_temp_state_reset(void) {
    s_heartbeat = 0u;
}

uint32_t app_temp_state_step(void) {
    ++s_heartbeat;
    return s_heartbeat;
}

uint32_t app_temp_state_get_heartbeat(void) {
    return s_heartbeat;
}
