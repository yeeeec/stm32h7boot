#ifndef FIRMWARE_CLOCK_PORT_H
#define FIRMWARE_CLOCK_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        void *context;
        uint32_t (*now_ms)(void *context);
    } clock_port_t;

#ifdef __cplusplus
}
#endif

#endif
