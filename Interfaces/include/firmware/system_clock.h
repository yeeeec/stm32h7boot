#ifndef FIRMWARE_SYSTEM_CLOCK_H
#define FIRMWARE_SYSTEM_CLOCK_H

#include <stdint.h>

typedef uint32_t (*system_clock_now_ms_fn)(void *context);

typedef struct
{
    void *context;
    system_clock_now_ms_fn now_ms;
} system_clock_t;

#endif
