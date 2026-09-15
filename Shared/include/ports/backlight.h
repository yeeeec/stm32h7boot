#ifndef FIRMWARE_BACKLIGHT_PORT_H
#define FIRMWARE_BACKLIGHT_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        void *context;
        firmware_status_t (*init)(void *context);
        firmware_status_t (*precent)(void *context, uint16_t precent);
        firmware_status_t (*onoff)(void *context, bool onoff);
    } backlight_port_t;

#ifdef __cplusplus
}
#endif

#endif
