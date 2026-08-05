#ifndef FIRMWARE_WATCHDOG_H
#define FIRMWARE_WATCHDOG_H

#include "firmware/status.h"

typedef firmware_status_t (*watchdog_refresh_fn)(void *context);

typedef struct
{
    void *context;
    watchdog_refresh_fn refresh;
} watchdog_t;

#endif
