#ifndef PLATFORM_TIME_H
#define PLATFORM_TIME_H

#include <stdint.h>

uint32_t Platform_TimeNowMs(void);
int Platform_TimeElapsed(uint32_t start_ms, uint32_t duration_ms);

#endif
