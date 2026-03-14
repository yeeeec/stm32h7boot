#ifndef LOG_BACKEND_RTT_H
#define LOG_BACKEND_RTT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void log_backend_rtt_init(void);
void log_backend_rtt_output(const char *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif
