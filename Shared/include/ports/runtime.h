#ifndef FIRMWARE_RUNTIME_PORT_H
#define FIRMWARE_RUNTIME_PORT_H

#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        void *context;
        uint32_t (*now_ms)(void *context);
        firmware_status_t (*kick_watchdog)(void *context);
    } runtime_port_t;

#ifdef __cplusplus
}
#endif

#endif
