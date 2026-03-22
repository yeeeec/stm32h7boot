#ifndef LOG_BACKEND_RTT_H
#define LOG_BACKEND_RTT_H

#include <stdint.h>

void log_backend_rtt_init(void);
void log_backend_rtt_output(const char *data, uint16_t len);

#endif