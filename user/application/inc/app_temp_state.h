#ifndef USER_APPLICATION_APP_TEMP_STATE_H
#define USER_APPLICATION_APP_TEMP_STATE_H

#include <stdint.h>

void app_temp_state_reset(void);
uint32_t app_temp_state_step(void);
uint32_t app_temp_state_get_heartbeat(void);

#endif
