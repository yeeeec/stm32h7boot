#ifndef FIRMWARE_BACKLIGHT_PORT_H
#define FIRMWARE_BACKLIGHT_PORT_H

#include <stdbool.h>
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
    /* Historical spelling is part of the shared App/Boot source contract. */
    firmware_status_t (*precent)(void *context, uint16_t percent);
    firmware_status_t (*onoff)(void *context, bool onoff);
} backlight_port_t;

#ifdef __cplusplus
}
#endif

#endif
