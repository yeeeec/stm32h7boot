#ifndef USER_SERVICES_SVC_TEMP_STATE_H
#define USER_SERVICES_SVC_TEMP_STATE_H

#include <stdint.h>

void svc_temp_state_reset(void);
void svc_temp_state_set_poll_count(uint32_t poll_count);
uint32_t svc_temp_state_get_poll_count(void);

#endif
